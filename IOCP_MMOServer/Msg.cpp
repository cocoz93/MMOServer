//////////////////////////////////////////////////////////////////////////
// CPP
//////////////////////////////////////////////////////////////////////////

#include "Msg.h"
#pragma warning(push) //realloc 경고 제거
#pragma warning(disable:6308) //realloc 경고 제거

//CFreeList<CMsg> CMsg::_MsgFreeList;
//CLockFree_FreeList<CMsg>* CMsg::_MsgFreeList;
CTLS_LockFree_FreeList<CMsg>* CMsg::_TlsMsgFreeList;

CMsg* CMsg::Alloc()
{
	//profile.Begin((WCHAR*)L"CMsg::Alloc");
	//________________________________________________________


	//0. new사용 
	//CMsg* msg = new CMsg;

	//1. LockFree-FreeList사용
	//CMsg* msg = _MsgFreeList->Alloc();

	//2. Tls-LockFree-FreeList사용
	CMsg* msg = _TlsMsgFreeList->Alloc();
	//________________________________________________________

	msg->Clear();
	//profile.End((WCHAR*)L"CMsg::Alloc");

	return msg;
}



void CMsg::Free(CMsg* msg)
{
	//profile.Begin((WCHAR*)L"CMsg::Free");
	//________________________________________________________

	//0. new사용 
	//delete msg;

	//1. LockFree-FreeList사용r
	//_MsgFreeList->Free(msg);

	//2. Tls-LockFree-FreeList사용
	_TlsMsgFreeList->Free(msg);
	//________________________________________________________


	//profile.End((WCHAR*)L"CMsg::Free");
}



void CMsg::AddRef()
{
	++this->_RefCount;
}



void CMsg::SubRef()
{
	if (0 == --this->_RefCount)
		this->Free(this);
	else
	{
		int err = 0;
	}
}



CMsg::CMsg()
{
	Initalize(MSG_DEFAULT_SIZE);
}

CMsg::CMsg(int BufferSize)
{
	Initalize(BufferSize);
}

CMsg::~CMsg()
{
	Release();
}

void CMsg::Initalize(int BufferSize)
{
	_Buff = new char[BufferSize];
	_BufferSize = BufferSize;

	this->Clear();
	//this->Setheader();
}

//공간이 부족할시 2배씩 늘린다.
bool CMsg::Addalloc(int addSize)
{
	int NewBufferSize = _BufferSize;
	while (true)
	{
		if ((NewBufferSize *= 2) > _BufferSize + addSize)
			break;
	}

	this->_Buff = (char*)realloc(this->_Buff, _BufferSize);
	_BufferSize = NewBufferSize;

	if (_Buff == NULL)
		return FALSE;
	else
		return TRUE;
}

void CMsg::Release(void)
{
	delete[] _Buff;
}

void CMsg::Clear(void)
{
	this->_front = 0;
	this->_rear = 0;
	this->_DataSize = 0;
	this->_RefCount = 1;
}

int CMsg::MoveWritePos(int size)
{
	//어차피 바깥에서 셋팅하고 Pos를 이동시키기문에,
	//여기서 공간이 부족할까봐 AddAlloc하는 로직이 들어가지않아도 된다
	_rear += size;
	_DataSize += size;

	return size;
}

int CMsg::MoveReadPos(int size)
{
	// 만약 size만큼 이동하려했는데, 
	// 이동할 front가 가진 데이터보다 더 뒤로 가는경우
	// DataSize에서 멈추도록 한다.
	if (_front + size > _rear)
	{
		_front += _DataSize;
		_DataSize = 0;
	}
	else
	{
		_front += size;
		_DataSize -= size;
	}
	return size;
}

//딱히 필요없음
//bool CMsg::Checkheader()
//{
//	//short Len = 0;
//	//memcpy_s(&Len, HEADER_SIZE, _Buff, HEADER_SIZE);
//	if ((SHORT)(*(SHORT*)(this->_Buff)) == PAYLOAD_SIZE)
//		return TRUE;
//	else return FALSE;
//}



//CMsg끼리 대입할 상황..
CMsg& CMsg::operator=(CMsg& SrcMsg)
{
	if (SrcMsg.GetDataSize() > this->GetDataSize())
		this->Addalloc(SrcMsg.GetDataSize());
	else
	{
		memcpy_s(this->_Buff, _BufferSize, SrcMsg._Buff, SrcMsg._DataSize);
		this->_DataSize = SrcMsg._DataSize;
	}
	return *this;
}




CMsg& CMsg::operator<<(char* Value)
{
	//0을 포함한다.
	SetData((char*)Value, (int)strlen(Value));
	return *this;
}

CMsg& CMsg::operator<<(BYTE Value)
{
	SetData((char*)&Value, sizeof(Value));
	return *this;
}

CMsg& CMsg::operator<<(char Value)
{
	SetData((char*)&Value, sizeof(Value));
	return *this;
}

CMsg& CMsg::operator<<(short Value)
{
	SetData((char*)&Value, sizeof(Value));
	return *this;
}

CMsg& CMsg::operator<<(WORD Value)
{
	SetData((char*)&Value, sizeof(Value));
	return *this;
}

CMsg& CMsg::operator<<(int Value)
{
	SetData((char*)&Value, sizeof(Value));
	return *this;
}

CMsg& CMsg::operator<<(DWORD Value)
{
	SetData((char*)&Value, sizeof(Value));
	return *this;
}

CMsg& CMsg::operator<<(float Value)
{
	SetData((char*)&Value, sizeof(Value));
	return *this;
}

CMsg& CMsg::operator<<(__int64 Value)
{
	SetData((char*)&Value, sizeof(Value));
	return *this;
}

CMsg& CMsg::operator<<(UINT64 Value)
{
	SetData((char*)&Value, sizeof(Value));
	return *this;
}

CMsg& CMsg::operator<<(double Value)
{
	SetData((char*)&Value, sizeof(Value));
	return *this;
}




CMsg& CMsg::operator>>(BYTE& Value)
{
	GetData((char*)&Value, sizeof(Value));
	return *this;
}

CMsg& CMsg::operator>>(char& Value)
{
	GetData((char*)&Value, sizeof(Value));
	return *this;
}

CMsg& CMsg::operator>>(short& Value)
{
	GetData((char*)&Value, sizeof(Value));
	return *this;
}

CMsg& CMsg::operator>>(WORD& Value)
{
	GetData((char*)&Value, sizeof(Value));
	return *this;
}

CMsg& CMsg::operator>>(int& Value)
{
	GetData((char*)&Value, sizeof(Value));
	return *this;
}

CMsg& CMsg::operator>>(DWORD& Value)
{
	GetData((char*)&Value, sizeof(Value));
	return *this;
}

CMsg& CMsg::operator>>(float& Value)
{
	GetData((char*)&Value, sizeof(Value));
	return *this;
}

CMsg& CMsg::operator>>(__int64& Value)
{
	GetData((char*)&Value, sizeof(Value));
	return *this;
}

CMsg& CMsg::operator>>(UINT64& Value)
{
	GetData((char*)&Value, sizeof(Value));
	return *this;
}

CMsg& CMsg::operator>>(double& Value)
{
	GetData((char*)&Value, sizeof(Value));
	return *this;
}

int CMsg::GetData(char* Dest, int size)
{
	if (IsEmpty(size) == TRUE)
		return 0;

	memcpy_s(Dest, size, _Buff + HEADER_SIZE + _front, size);
	_front += size;
	_DataSize -= size;
	return size;
}

int CMsg::PeekData(char* Dest, int size)
{
	if (IsEmpty(size) == TRUE)
		return 0;

	memcpy_s(Dest, size, _Buff + HEADER_SIZE + _front, size);
	return size;
}

int CMsg::SetData(char* Src, int size)
{
	if (IsFull(size))
		this->Addalloc(size);

	memcpy_s(_Buff + HEADER_SIZE + _rear, _BufferSize, Src, size);
	_rear += size;
	_DataSize += size;
	return size;
}

#pragma warning(pop)