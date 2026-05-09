/////////////////////////////////////////////////////////
// header
/////////////////////////////////////////////////////////

#pragma once

#ifndef ____MSG_H____
#define ____MSG_H____

#include "LockFree/ExternalTlsFreeList.h"

static constexpr int MSG_DEFAULT_SIZE = 1400;
static constexpr int HEADER_SIZE = 2;


class CSerialBuffer
{
public:
	static CExternalTlsFreeList<CSerialBuffer>* _TlsMsgFreeList;

public:
	static CSerialBuffer* Alloc();
	static void Free(CSerialBuffer* msg);
	void AddRef();
	void SubRef();


public:
	//private:
	explicit CSerialBuffer();
	explicit CSerialBuffer(int BufferSize);
	virtual	~CSerialBuffer();

	CSerialBuffer(const CSerialBuffer&) = delete;
	CSerialBuffer(CSerialBuffer&&) = delete;

public:
	void Initialize(int BufferSize);		// 메모리풀 할당
	void Release(void); //Msg 해제
	void Clear(void);   //Msg 초기화 (메모리풀로 사용할때 호출)

public:
	int	GetBufferSize(void) { return _BufferSize; }

	//헤더를 제외한, 실제 컨텐츠쪽에서 사용하고있는 사이즈
	int	GetDataSize(void) { return _DataSize; }
	char* GetReadBufferPtr(void) { return _Buff + HEADER_SIZE + _front; }
	char* GetWriteBufferPtr(void) { return _Buff + HEADER_SIZE + _rear; }
	int	MoveWritePos(int size);
	int	MoveReadPos(int size);

	//헤더부분 접근금지
public:
	char* GetHeaderBufferPtr(void) { return _Buff; }
	char* GetPayloadBufferPtr(void) { return _Buff + HEADER_SIZE; }

public:
	bool IsFull(int size) { return _DataSize + size > _BufferSize - HEADER_SIZE; }
	bool IsEmpty(int size) { return size > _DataSize; }
	bool IsSealed() { return _Sealed; }

	//헤더세팅은 바깥에서 막기위함
public:
	//bool Checkheader();


public:
	CSerialBuffer& operator = (const CSerialBuffer& SrcMsg);


	//Input
public:
	CSerialBuffer& operator << (const char* Value);
	CSerialBuffer& operator << (const WCHAR* Value);

	CSerialBuffer& operator << (BYTE Value);
	CSerialBuffer& operator << (char Value);

	CSerialBuffer& operator << (short Value);
	CSerialBuffer& operator << (WORD Value);

	CSerialBuffer& operator << (int Value);
	CSerialBuffer& operator << (DWORD Value);

	CSerialBuffer& operator << (INT64 Value);
	CSerialBuffer& operator << (UINT64 Value);


	CSerialBuffer& operator << (float Value);
	CSerialBuffer& operator << (double Value);

	//Output
public:
	//CSerialBuffer& operator >> (char* Value);

	CSerialBuffer& operator >> (BYTE& Value);
	CSerialBuffer& operator >> (char& Value);

	CSerialBuffer& operator >> (short& Value);
	CSerialBuffer& operator >> (WORD& Value);

	CSerialBuffer& operator >> (int& Value);
	CSerialBuffer& operator >> (DWORD& Value);

	CSerialBuffer& operator >> (INT64& Value);
	CSerialBuffer& operator >> (UINT64& Value);

	CSerialBuffer& operator >> (float& Value);
	CSerialBuffer& operator >> (double& Value);

public:
	int	GetData(char* Dest, int size);
	int PeekData(char* Dest, int size);
	int	SetData(char* Src, int size);


private:
	char*		_Buff;
	int			_BufferSize;
	int			_DataSize;
	int			_front = 0;
	int			_rear = 0;
	bool		_Sealed = false;

public:
	LONG64		_RefCount;
};

// 8 + 4 + 4 + 4 + 4 + 4


#endif// ____MSG_H____