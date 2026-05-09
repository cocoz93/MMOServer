//////////////////////////////////////////////////////////////////////////
// CPP
//////////////////////////////////////////////////////////////////////////

#include "SerialBuffer.h"

//CFreeList<CSerialBuffer> CSerialBuffer::_MsgFreeList;
//CLockFree_FreeList<CSerialBuffer>* CSerialBuffer::_MsgFreeList;
CExternalTlsFreeList<CSerialBuffer>* CSerialBuffer::_TlsMsgFreeList;

CSerialBuffer* CSerialBuffer::Alloc()
{
	//profile.Begin((WCHAR*)L"CSerialBuffer::Alloc");
	//________________________________________________________


	//0. new사용 
	//CSerialBuffer* msg = new CSerialBuffer;

	//1. LockFree-FreeList사용
	//CSerialBuffer* msg = _MsgFreeList->Alloc();

	//2. Tls-LockFree-FreeList사용
	CSerialBuffer* msg = _TlsMsgFreeList->Alloc();
	//________________________________________________________

	msg->Clear();
	//profile.End((WCHAR*)L"CSerialBuffer::Alloc");

	return msg;
}



void CSerialBuffer::Free(CSerialBuffer* msg)
{
	//profile.Begin((WCHAR*)L"CSerialBuffer::Free");
	//________________________________________________________

	//0. new사용 
	//delete msg;

	//1. LockFree-FreeList사용r
	//_MsgFreeList->Free(msg);

	//2. Tls-LockFree-FreeList사용
	_TlsMsgFreeList->Free(msg);
	//________________________________________________________


	//profile.End((WCHAR*)L"CSerialBuffer::Free");
}



void CSerialBuffer::AddRef()
{
	_Sealed = true;
	InterlockedIncrement64(&_RefCount);
}



void CSerialBuffer::SubRef()
{
	if (0 == InterlockedDecrement64(&_RefCount))
		Free(this);
}



CSerialBuffer::CSerialBuffer()
{
	Initialize(MSG_DEFAULT_SIZE);
}

CSerialBuffer::CSerialBuffer(int BufferSize)
{
	Initialize(BufferSize);
}

CSerialBuffer::~CSerialBuffer()
{
	Release();
}

void CSerialBuffer::Initialize(int BufferSize)
{
	_Buff = new char[BufferSize];
	_BufferSize = BufferSize;

	this->Clear();
}

void CSerialBuffer::Release(void)
{
	delete[] _Buff;
}

void CSerialBuffer::Clear(void)
{
	this->_front = 0;
	this->_rear = 0;
	this->_DataSize = 0;
	this->_RefCount = 0;
	this->_Sealed = false;
}

int CSerialBuffer::MoveWritePos(int size)
{
	//어차피 바깥에서 셋팅하고 Pos를 이동시키기문에,
	//여기서 공간이 부족할까봐 AddAlloc하는 로직이 들어가지않아도 된다
	if (IsFull(size))
		return 0;

	_rear += size;
	_DataSize += size;

	return size;
}

int CSerialBuffer::MoveReadPos(int size)
{
	// 만약 size만큼 이동하려했는데, 
	// 이동할 front가 가진 데이터보다 더 뒤로 가는경우
	// DataSize에서 멈추도록 한다.
	if (_front + size > _rear)
	{
		int moved = _DataSize;
		_front += _DataSize;
		_DataSize = 0;
		return moved;
	}
	else
	{
		_front += size;
		_DataSize -= size;
		return size;
	}
}

//딱히 필요없음
//bool CSerialBuffer::Checkheader()
//{
//	//short Len = 0;
//	//memcpy_s(&Len, HEADER_SIZE, _Buff, HEADER_SIZE);
//	if ((SHORT)(*(SHORT*)(this->_Buff)) == PAYLOAD_SIZE)
//		return TRUE;
//	else return FALSE;
//}



CSerialBuffer& CSerialBuffer::operator=(const CSerialBuffer& SrcMsg)
{
	if (this == &SrcMsg)
		return *this;

	int srcTotalSize = HEADER_SIZE + SrcMsg._DataSize;
	if (srcTotalSize > _BufferSize)
		return *this;

	memcpy_s(_Buff, _BufferSize, SrcMsg._Buff, srcTotalSize);
	_DataSize = SrcMsg._DataSize;
	_front = SrcMsg._front;
	_rear = SrcMsg._rear;
	_Sealed = SrcMsg._Sealed;
	return *this;
}




CSerialBuffer& CSerialBuffer::operator<<(const char* Value)
{
	short Len = (short)strlen(Value);
	SetData((char*)&Len, sizeof(Len));
	SetData((char*)Value, Len);
	return *this;
}

CSerialBuffer& CSerialBuffer::operator<<(const WCHAR* Value)
{
	short Len = (short)(wcslen(Value) * sizeof(WCHAR));
	SetData((char*)&Len, sizeof(Len));
	SetData((char*)Value, Len);
	return *this;
}

CSerialBuffer& CSerialBuffer::operator<<(BYTE Value)
{
	SetData((char*)&Value, sizeof(Value));
	return *this;
}

CSerialBuffer& CSerialBuffer::operator<<(char Value)
{
	SetData((char*)&Value, sizeof(Value));
	return *this;
}

CSerialBuffer& CSerialBuffer::operator<<(short Value)
{
	SetData((char*)&Value, sizeof(Value));
	return *this;
}

CSerialBuffer& CSerialBuffer::operator<<(WORD Value)
{
	SetData((char*)&Value, sizeof(Value));
	return *this;
}

CSerialBuffer& CSerialBuffer::operator<<(int Value)
{
	SetData((char*)&Value, sizeof(Value));
	return *this;
}

CSerialBuffer& CSerialBuffer::operator<<(DWORD Value)
{
	SetData((char*)&Value, sizeof(Value));
	return *this;
}

CSerialBuffer& CSerialBuffer::operator<<(float Value)
{
	SetData((char*)&Value, sizeof(Value));
	return *this;
}

CSerialBuffer& CSerialBuffer::operator<<(INT64 Value)
{
	SetData((char*)&Value, sizeof(Value));
	return *this;
}

CSerialBuffer& CSerialBuffer::operator<<(UINT64 Value)
{
	SetData((char*)&Value, sizeof(Value));
	return *this;
}

CSerialBuffer& CSerialBuffer::operator<<(double Value)
{
	SetData((char*)&Value, sizeof(Value));
	return *this;
}




CSerialBuffer& CSerialBuffer::operator>>(BYTE& Value)
{
	if (0 == GetData((char*)&Value, sizeof(Value)))
		Value = 0;
	return *this;
}

CSerialBuffer& CSerialBuffer::operator>>(char& Value)
{
	if (0 == GetData((char*)&Value, sizeof(Value)))
		Value = 0;
	return *this;
}

CSerialBuffer& CSerialBuffer::operator>>(short& Value)
{
	if (0 == GetData((char*)&Value, sizeof(Value)))
		Value = 0;
	return *this;
}

CSerialBuffer& CSerialBuffer::operator>>(WORD& Value)
{
	if (0 == GetData((char*)&Value, sizeof(Value)))
		Value = 0;
	return *this;
}

CSerialBuffer& CSerialBuffer::operator>>(int& Value)
{
	if (0 == GetData((char*)&Value, sizeof(Value)))
		Value = 0;
	return *this;
}

CSerialBuffer& CSerialBuffer::operator>>(DWORD& Value)
{
	if (0 == GetData((char*)&Value, sizeof(Value)))
		Value = 0;
	return *this;
}

CSerialBuffer& CSerialBuffer::operator>>(float& Value)
{
	if (0 == GetData((char*)&Value, sizeof(Value)))
		Value = 0;
	return *this;
}

CSerialBuffer& CSerialBuffer::operator>>(INT64& Value)
{
	if (0 == GetData((char*)&Value, sizeof(Value)))
		Value = 0;
	return *this;
}

CSerialBuffer& CSerialBuffer::operator>>(UINT64& Value)
{
	if (0 == GetData((char*)&Value, sizeof(Value)))
		Value = 0;
	return *this;
}

CSerialBuffer& CSerialBuffer::operator>>(double& Value)
{
	if (0 == GetData((char*)&Value, sizeof(Value)))
		Value = 0;
	return *this;
}

int CSerialBuffer::GetData(char* Dest, int size)
{
	if (_Sealed || IsEmpty(size))
		return 0;

	memcpy_s(Dest, size, _Buff + HEADER_SIZE + _front, size);
	_front += size;
	_DataSize -= size;
	return size;
}

int CSerialBuffer::PeekData(char* Dest, int size)
{
	if (IsEmpty(size))
		return 0;

	memcpy_s(Dest, size, _Buff + HEADER_SIZE + _front, size);
	return size;
}

int CSerialBuffer::SetData(char* Src, int size)
{
	if (_Sealed || IsFull(size))
		return 0;

	memcpy_s(_Buff + HEADER_SIZE + _rear, _BufferSize - HEADER_SIZE - _rear, Src, size);
	_rear += size;
	_DataSize += size;
	return size;
}