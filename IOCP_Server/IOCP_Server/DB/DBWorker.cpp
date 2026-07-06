#include "DBWorker.h"

#if USE_DB_WORKER

#include <winsock2.h>   // mysql.h 이전에 — 구버전 winsock 자동 include 충돌 차단
#include <mysql.h>
#include <errmsg.h>     // CR_SERVER_GONE_ERROR / CR_SERVER_LOST (커넥션 유실 감지)
#include <cstdio>
#include <chrono>

#include "../MonitorManager.h"
#include "../../../Shared/Common/ErrorLog.h"

#pragma comment(lib, "libmysql.lib")

CDBWorker::CDBWorker(CMonitorManager& monitor)
    : _monitor(monitor)
{
}

CDBWorker::~CDBWorker()
{
    Shutdown(0);
}

bool CDBWorker::Start(const DBConfig& config, int workerCount, int queueMax)
{
    _config      = config;
    _workerCount = (workerCount > 0) ? workerCount : 1;
    if (_workerCount > CMonitorManager::MAX_DB_WORKERS)
        _workerCount = CMonitorManager::MAX_DB_WORKERS;   // 지표 배열 상한에 맞춤
    _queueMax    = (queueMax > 0) ? queueMax : 20000;

    // 멀티스레드 안전 초기화 — 워커 스레드 생성 "전"에 메인에서 1회
    if (mysql_library_init(0, nullptr, nullptr) != 0)
    {
        SLOG_ERROR("[DB] mysql_library_init failed");
        return false;
    }
    _libInited = true;   // 이후 어느 경로로 실패하든 library_end로 롤백 (짝 맞춤)

    // K개 커넥션을 메인 스레드에서 생성(=fail-fast). 하나라도 실패하면 이미 연 것 정리 후 false.
    _workers.reserve(_workerCount);
    for (int i = 0; i < _workerCount; ++i)
    {
        auto slot = std::make_unique<DBWorkerSlot>();
        slot->index = i;
        if (!Connect(slot->mysql) || !PrepareStmt(*slot))
        {
            SLOG_ERROR("[DB] worker {} connect/prepare failed", i);
            if (slot->stmt)  { mysql_stmt_close(static_cast<MYSQL_STMT*>(slot->stmt)); slot->stmt = nullptr; }
            if (slot->mysql) { mysql_close(static_cast<MYSQL*>(slot->mysql)); slot->mysql = nullptr; }
            for (auto& s : _workers)
            {
                if (s->stmt)  { mysql_stmt_close(static_cast<MYSQL_STMT*>(s->stmt)); s->stmt = nullptr; }
                if (s->mysql) { mysql_close(static_cast<MYSQL*>(s->mysql)); s->mysql = nullptr; }
            }
            _workers.clear();
            mysql_library_end();       // library_init 롤백 (Start 실패 시 누수 방지)
            _libInited = false;
            return false;
        }
        _workers.push_back(std::move(slot));
    }

    _perWorker.assign(_workerCount, {});
    _monitor._dbWorkerCount = static_cast<LONG>(_workerCount);   // 지표 노출 상한

    // 커넥션 확보 후 워커 스레드 시작 (각 슬롯 커넥션은 해당 워커만 단독 사용)
    _stop.store(false);
    for (int i = 0; i < _workerCount; ++i)
        _workers[i]->thread = std::thread(&CDBWorker::WorkerThread, this, i);

    _started = true;
    SLOG_INFO("[DB] connected {}:{} db={} — {} worker(s), queueMax={}",
              _config.host, _config.port, _config.database, _workerCount, _queueMax);
    return true;
}

bool CDBWorker::Connect(void*& outMysql)
{
    MYSQL* m = mysql_init(nullptr);
    if (m == nullptr)
    {
        SLOG_ERROR("[DB] mysql_init failed");
        return false;
    }

    unsigned int connTimeout = static_cast<unsigned int>(_config.connectTimeoutSec);
    mysql_options(m, MYSQL_OPT_CONNECT_TIMEOUT, &connTimeout);
    // 읽기/쓰기 타임아웃 — DB가 응답 없이 멈추면 쿼리가 무한 대기하지 않고 실패 반환(재연결 판단 가능)
    unsigned int rwTimeout = static_cast<unsigned int>(_config.rwTimeoutSec);
    mysql_options(m, MYSQL_OPT_READ_TIMEOUT, &rwTimeout);
    mysql_options(m, MYSQL_OPT_WRITE_TIMEOUT, &rwTimeout);
    mysql_options(m, MYSQL_SET_CHARSET_NAME, "utf8mb4");

    if (mysql_real_connect(m,
            _config.host.c_str(), _config.user.c_str(), _config.password.c_str(),
            _config.database.c_str(), static_cast<unsigned int>(_config.port),
            nullptr, 0) == nullptr)
    {
        SLOG_ERROR("[DB] connect failed: {}", mysql_error(m));
        mysql_close(m);
        return false;
    }

    outMysql = m;
    return true;
}

void CDBWorker::PushToSlot(DBWorkerSlot& slot, const DBSaveJob* jobs, size_t count)
{
    if (count == 0)
        return;

    size_t take = 0;
    {
        std::lock_guard<std::mutex> lk(slot.mutex);
        const size_t cur  = slot.queue.size();
        const size_t cap  = static_cast<size_t>(_queueMax);
        const size_t room = (cur < cap) ? (cap - cur) : 0;
        take = (count <= room) ? count : room;    // 백프레셔: 여유만큼만 수용
        if (take > 0)
            slot.queue.insert(slot.queue.end(), jobs, jobs + take);
    }

    if (take > 0)
        slot.cv.notify_one();                     // 실제로 넣었을 때만 기상

    const size_t dropped = count - take;
    if (dropped > 0)
    {
        InterlockedExchangeAdd64(&_monitor._dbDroppedJobs, static_cast<LONG64>(dropped));
        SLOG_ERROR("[DB] backpressure drop — worker={} dropped={} (queue full, max={})",
                   slot.index, dropped, _queueMax);
    }
}

void CDBWorker::Enqueue(const DBSaveJob& job)
{
    PushToSlot(*_workers[SlotIndex(job.accountId)], &job, 1);
}

void CDBWorker::EnqueueBatch(const std::vector<DBSaveJob>& batch)
{
    if (batch.empty())
        return;

    // 단일 워커: 분류 생략, 통째 push (K=1 fast path)
    if (_workerCount == 1)
    {
        PushToSlot(*_workers[0], batch.data(), batch.size());
        return;
    }

    // K>1: accountId%K로 워커별 분류 후 슬롯당 1회 핸드오프
    for (auto& v : _perWorker)
        v.clear();
    for (const DBSaveJob& job : batch)
        _perWorker[SlotIndex(job.accountId)].push_back(job);
    for (int i = 0; i < _workerCount; ++i)
        if (!_perWorker[i].empty())
            PushToSlot(*_workers[i], _perWorker[i].data(), _perWorker[i].size());
}

void CDBWorker::WorkerThread(int idx)
{
    mysql_thread_init();

    DBWorkerSlot& slot = *_workers[idx];

    std::vector<DBSaveJob> local;
    while (true)
    {
        {
            std::unique_lock<std::mutex> lk(slot.mutex);
            slot.cv.wait(lk, [this, &slot] { return !slot.queue.empty() || _stop.load(); });
            if (slot.queue.empty() && _stop.load())
                break;                   // 정지 요청 + 잔여 없음 → 종료(드레인 완료)
            local.swap(slot.queue);       // 누적분 통째 인출 (여러 주기분 병합 가능)
        }

        // 배치 처리 전 — 커넥션이 끊겨 있으면 재연결 1회 시도.
        //   배치=저장 주기 단위라, 죽은 DB엔 주기마다 한 번만 재시도(자연스러운 백오프).
        if (slot.mysql == nullptr)
        {
            void* fresh = nullptr;
            if (Connect(fresh))
            {
                slot.mysql = fresh;
                if (PrepareStmt(slot))
                    SLOG_INFO("[DB] worker {} reconnected", idx);
                else
                {
                    mysql_close(static_cast<MYSQL*>(slot.mysql));   // stmt 준비 실패 → 커넥션 롤백
                    slot.mysql = nullptr;
                }
            }
            // 실패하면 이번 배치는 전부 실패 처리되고, 다음 배치에서 다시 시도
        }

        _monitor._dbQueueDepth[idx] = static_cast<LONG64>(local.size());  // 워커별 게이지

        for (const DBSaveJob& job : local)
        {
            if (ExecSave(slot, job))   // 유실 시 ExecSave가 stmt/mysql을 null로 만듦
                InterlockedIncrement64(&_monitor._dbSavedJobs);
            else
                InterlockedIncrement64(&_monitor._dbFailedJobs);
        }
        local.clear();
    }

    mysql_thread_end();
}

bool CDBWorker::PrepareStmt(DBWorkerSlot& slot)
{
    MYSQL* m = static_cast<MYSQL*>(slot.mysql);
    MYSQL_STMT* st = mysql_stmt_init(m);
    if (st == nullptr)
    {
        SLOG_ERROR("[DB] stmt_init failed: {}", mysql_error(m));
        return false;
    }

    // VALUES 4개 + ON DUPLICATE KEY UPDATE 3개 = ? 7개 (x·y·map_id는 양쪽이 같은 버퍼 참조)
    static const char SQL[] =
        "INSERT INTO characters (account_id,x,y,map_id) VALUES (?,?,?,?) "
        "ON DUPLICATE KEY UPDATE x=?,y=?,map_id=?";
    if (mysql_stmt_prepare(st, SQL, sizeof(SQL) - 1) != 0)
    {
        SLOG_ERROR("[DB] stmt_prepare failed: {}", mysql_stmt_error(st));
        mysql_stmt_close(st);
        return false;
    }

    // 파라미터 바인딩 — 버퍼는 슬롯 멤버(execute 직전 값만 갱신). MYSQL_TYPE_LONG=32bit, LONGLONG=64bit.
    MYSQL_BIND b[7] = {};
    b[0].buffer_type = MYSQL_TYPE_LONGLONG; b[0].buffer = &slot.bAccountId;
    b[1].buffer_type = MYSQL_TYPE_FLOAT;    b[1].buffer = &slot.bX;
    b[2].buffer_type = MYSQL_TYPE_FLOAT;    b[2].buffer = &slot.bY;
    b[3].buffer_type = MYSQL_TYPE_LONG;     b[3].buffer = &slot.bMapId;
    b[4] = b[1];  // UPDATE x      = 같은 bX
    b[5] = b[2];  // UPDATE y      = 같은 bY
    b[6] = b[3];  // UPDATE map_id = 같은 bMapId
    if (mysql_stmt_bind_param(st, b) != 0)
    {
        SLOG_ERROR("[DB] stmt_bind_param failed: {}", mysql_stmt_error(st));
        mysql_stmt_close(st);
        return false;
    }

    slot.stmt = st;
    return true;
}

bool CDBWorker::ExecSave(DBWorkerSlot& slot, const DBSaveJob& job)
{
    if (slot.mysql == nullptr || slot.stmt == nullptr)
        return false;   // 커넥션/stmt 없음(재연결 대기 중) — 즉시 실패, 무한 대기 없음

    // 바인딩 버퍼에 이번 잡 값 복사 (stmt는 이 버퍼 주소를 이미 가리킴 → execute만 하면 됨)
    slot.bAccountId = job.accountId;
    slot.bX         = job.x;
    slot.bY         = job.y;
    slot.bMapId     = job.mapId;

    MYSQL_STMT* st = static_cast<MYSQL_STMT*>(slot.stmt);
    if (mysql_stmt_execute(st) == 0)
        return true;

    // 저장 실패 — 커넥션 유실인지, 단순 오류(제약 위반 등)인지 구분
    const unsigned int err = mysql_stmt_errno(st);
    if (err == CR_SERVER_GONE_ERROR || err == CR_SERVER_LOST)
    {
        // 커넥션이 죽음 → stmt·커넥션 닫고 null로. 재연결은 WorkerThread가 다음 배치 시작 때 시도(백오프).
        SLOG_WARN("[DB] connection lost (errno={}) — will reconnect next batch", err);
        mysql_stmt_close(st);
        slot.stmt = nullptr;
        mysql_close(static_cast<MYSQL*>(slot.mysql));
        slot.mysql = nullptr;
    }
    else
    {
        SLOG_ERROR("[DB] save failed (account={}): {}", job.accountId, mysql_stmt_error(st));
    }
    return false;
}

void CDBWorker::Shutdown(int drainTimeoutSec)
{
    if (!_started)
        return;

    const auto t0 = std::chrono::steady_clock::now();

    // 모든 슬롯에 정지 신호 → 각 워커가 잔여 드레인 후 종료
    _stop.store(true);
    for (auto& s : _workers)
        s->cv.notify_one();

    for (auto& s : _workers)
        if (s->thread.joinable())
            s->thread.join();

    for (auto& s : _workers)
    {
        if (s->stmt)  { mysql_stmt_close(static_cast<MYSQL_STMT*>(s->stmt)); s->stmt = nullptr; }
        if (s->mysql) { mysql_close(static_cast<MYSQL*>(s->mysql)); s->mysql = nullptr; }
    }

    _workers.clear();

    if (_libInited)                    // library_init과 짝 — 모든 커넥션·스레드 정리 후 1회
    {
        mysql_library_end();
        _libInited = false;
    }
    _started = false;

    const auto drainMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    SLOG_INFO("[DB] {} worker(s) drained and stopped ({} ms)", _workerCount, drainMs);

    // 1단계: 워커가 잔여를 전부 비우고 종료하므로 타임아웃 미사용. 장애 대비 타임아웃은 3단계.
    (void)drainTimeoutSec;
}

#endif // USE_DB_WORKER
