/////////////////////////////////////////////////////////
// header
/////////////////////////////////////////////////////////

#pragma once

#ifndef ____MSG_H____
#define ____MSG_H____


//#include "FreeList.h"
#include "TLS_LockFree_FreeList.h"
#include "TlsProfile.h"
#include "time.h"

#define MSG_DEFAULT_SIZE		1400

//디코딩용 헤더
#define MSG_CODE				119
#define MSG_KEY					50
#define HEADER_SIZE				(int)sizeof(NET_HEADER)  //2(LAN_HEADER)

#pragma pack(push, 1)
struct NET_HEADER
{
	BYTE	Code;
	SHORT	Len;
	BYTE	RandKey;
	BYTE	CheckSum;
};
#pragma pack(pop)


class CMsg
{
	//friend class CFreeList<CMsg>;
	//friend class CLockFree_FreeList<CMsg>;
	//friend class CTLS_LockFree_FreeList<CMsg>;
	friend class CNetServer;
	friend class CMyNetServer;

public:
	//static CFreeList<CMsg> _MsgFreeList;
	//static CLockFree_FreeList<CMsg>* _MsgFreeList;
	static CTLS_LockFree_FreeList<CMsg>* _TlsMsgFreeList;

public:
	static CMsg* Alloc();
	static void Free(CMsg* msg);
	void AddRef();
	void SubRef();


public:
	//private:
	explicit CMsg();
	explicit CMsg(int BufferSize);
	virtual	~CMsg();

public:
	void Initalize(int BufferSize);		// 메모리풀 할당
	bool Addalloc(int TotalSize);
	void Release(void); //Msg 해제
	void Clear(void);   //Msg 초기화 (메모리풀로 사용할때 호출)

public:
	int	GetBufferSize(void) { return _BufferSize; }


	//헤더를 제외한, 실제 컨텐츠쪽에서 사용하고있는 사이즈
	int	GetDataSize(void) 
	{
	
		if (_DataSize <= 0)
		{
			int err = 0;
		}

		return _DataSize;
	
	}
	char* GetFrontBufferPtr(void) { return _Buff + HEADER_SIZE + _front; }
	char* GetRearBufferPtr(void) { return _Buff + HEADER_SIZE + _rear; }
	int	MoveWritePos(int size);
	int	MoveReadPos(int size);

	//헤더부분 접근금지
public:
	char* GetMsgBufferPtr(void) { return _Buff; }
	char* GetMsgDataBufferPtr(void) { return _Buff + HEADER_SIZE; }

public:
	bool IsFull(int size) { return _DataSize + size >= _BufferSize; }
	bool IsEmpty(int size) { return _DataSize - size < 0; }

	//헤더세팅은 바깥에서 막기위함
private:


public:
	//bool Checkheader();


public:
	CMsg& operator = (CMsg& SrcMsg);


	//Input
public:
	CMsg& operator << (char* Value);

	CMsg& operator << (BYTE Value);
	CMsg& operator << (char Value);

	CMsg& operator << (short Value);
	CMsg& operator << (WORD Value);

	CMsg& operator << (int Value);
	CMsg& operator << (DWORD Value);

	CMsg& operator << (__int64 Value);
	CMsg& operator << (UINT64 Value);


	CMsg& operator << (float Value);
	CMsg& operator << (double Value);




	//Output
public:
	//CMsg& operator >> (char* Value);

	CMsg& operator >> (BYTE& Value);
	CMsg& operator >> (char& Value);

	CMsg& operator >> (short& Value);
	CMsg& operator >> (WORD& Value);

	CMsg& operator >> (int& Value);
	CMsg& operator >> (DWORD& Value);

	CMsg& operator >> (__int64& Value);
	CMsg& operator >> (UINT64& Value);


	CMsg& operator >> (float& Value);
	CMsg& operator >> (double& Value);



public:
	int	GetData(char* Dest, int size);
	int PeekData(char* Dest, int size);
	int	SetData(char* Src, int size);

	char*		_Buff;

private:
	volatile __int64	_BufferSize;
	volatile __int64	_DataSize;
	volatile __int64	_front = 0;
	volatile __int64	_rear = 0;

public:
	volatile __int64		_RefCount;
};

// 8 + 4 + 4 + 4 + 4 + 4


#endif// ____MSG_H____