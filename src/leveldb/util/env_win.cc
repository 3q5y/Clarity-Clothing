// This file contains source that originates from:
// http://code.google.com/p/leveldbwin/source/browse/trunk/win32_impl_src/env_win32.h
// http://code.google.com/p/leveldbwin/source/browse/trunk/win32_impl_src/port_win32.cc
// Those files don't have any explicit license headers but the 
// project (http://code.google.com/p/leveldbwin/) lists the 'New BSD License'
// as the license.
#if defined(LEVELDB_PLATFORM_WINDOWS)
#include <map>


#include "leveldb/env.h"

#include "port/port.h"
#include "leveldb/slice.h"
#include "util/logging.h"

#include <shlwapi.h>
#include <process.h>
#include <cstring>
#include <stdio.h>
#include <errno.h>
#include <io.h>
#include <algorithm>

#ifdef max
#undef max
#endif

#ifndef va_copy
#define va_copy(d,s) ((d) = (s))
#endif

#if defined DeleteFile
#undef DeleteFile
#endif

//Declarations
namespace leveldb
{

namespace Win32
{

#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName&);               \
  void operator=(const TypeName&)

std::string GetCurrentDir();
std::wstring GetCurrentDirW();

static const std::string CurrentDir = GetCurrentDir();
static const std::wstring CurrentDirW = GetCurrentDirW();

std::string& ModifyPath(std::string& path);
std::wstring& ModifyPath(std::wstring& path);

std::string GetLastErrSz();
std::wstring GetLastErrSzW();

size_t GetPageSize();

typedef void (*ScheduleProc)(void*) ;

struct WorkItemWrapper
{
    WorkItemWrapper(ScheduleProc proc_,void* content_);
    ScheduleProc proc;
    void* pContent;
};

DWORD WINAPI WorkItemWrapperProc(LPVOID pContent);

class Win32SequentialFile : public SequentialFile
{
public:
    friend class Win32Env;
    virtual ~Win32SequentialFile();
    virtual Status Read(size_t n, Slice* result, char* scratch);
    virtual Status Skip(uint64_t n);
    BOOL isEnable();
    virtual std::string GetName() const { return _filename; }
private:
    BOOL _Init();
    void _CleanUp();
    Win32SequentialFile(const std::string& fname);
    std::string _filename;
    ::HANDLE _hFile;
    DISALLOW_COPY_AND_ASSIGN(Win32SequentialFile);
};

class Win32RandomAccessFile : public RandomAccessFile
{
public:
    friend class Win32Env;
    virtual ~Win32RandomAccessFile();
    virtual Status Read(uint64_t offset, size_t n, Slice* result,char* scratch) const;
    BOOL isEnable();
    virtual std::string GetName() const { return _filename; }
private:
    BOOL _Init(LPCWSTR path);
    void _CleanUp();
    Win32RandomAccessFile(const std::string& fname);
    HANDLE _hFile;
    const std::string _filename;
    DISALLOW_COPY_AND_ASSIGN(Win32RandomAccessFile);
};

class Win32WritableFile : public WritableFile
{
public:
    Win32WritableFile(const std::string& fname, bool append);
    ~Win32WritableFile();

    virtual Status Append(const Slice& data);
    virtual Status Close();
    virtual Status Flush();
    virtual Status Sync();
    BOOL isEnable();
    virtual std::string GetName() const { return filename_; }
private:
    std::string filename_;
    ::HANDLE _hFile;
};

class Win32FileLock : public FileLock
{
public:
    friend class Win32Env;
    virtual ~Win32FileLock();
    BOOL isEnable();
private:
    BOOL _Init(LPCWSTR path);
    void _CleanUp();
    Win32FileLock(const std::string& fname);
    HANDLE _hFile;
    std::string _filename;
    DISALLOW_COPY_AND_ASSIGN(Win32FileLock);
};

class Win32Logger : public Logger
{
public: 
    friend class Win32Env;
    virtual ~Win32Logger();
    virtual void Logv(const char* format, va_list ap);
private:
    explicit Win32Logger(WritableFile* pFile);
    WritableFile* _pFileProxy;
    DISALLOW_COPY_AND_ASSIGN(Win32Logger);
};

class Win32Env : public Env
{
public:
    Win32Env();
    virtual ~Win32Env();
    virtual Status NewSequentialFile(const std::string& fname,
        SequentialFile** result);

    virtual Status NewRandomAccessFile(const std::string& fname,
        RandomAccessFile** result);
    virtual Status NewWritableFile(const std::string& fname,
        WritableFile** result);
    virtual Status NewAppendableFile(const std::string& fname,
        WritableFile** result);

    virtual bool FileExists(const std::string& fname);

    virtual Status GetChildren(const std::string& dir,
        std::vector<std::string>* result);

    virtual Status DeleteFile(const std::string& fname);

    virtual Status CreateDir(const std::string& dirname);

    virtual Status DeleteDir(const std::string& dirname);

    virtual Status GetFileSize(const std::string& fname, uint64_t* file_size);

    virtual Status RenameFile(const std::string& src,
        const std::string& target);

    virtual Status LockFile(const std::string& fname, FileLock** lock);

    virtual Status UnlockFile(FileLock* lock);

    virtual void Schedule(
        void (*function)(void* arg),
        void* arg);

    virtual void StartThread(void (*function)(void* arg), void* arg);

    virtual Status GetTestDirectory(std::string* path);

    //virtual void Logv(WritableFile* log, const char* format, va_list ap);

    virtual Status NewLogger(const std::string& fname, Logger** result);

    virtual uint64_t NowMicros();

    virtual void SleepForMicroseconds(int micros);
};

void ToWidePath(const std::string& value, std::wstring& target) {
	wchar_t buffer[MAX_PATH];
	MultiByteToWideChar(CP_ACP, 0, value.c_str(), -1, buffer, MAX_PATH);
	target = buffer;
}

void ToNarrowPath(const std::wstring& value, std::string& target) {
	char buffer[MAX_PATH];
	WideCharToMultiByte(CP_ACP, 0, value.c_str(), -1, buffer, MAX_PATH, NULL, NULL);
	target = buffer;
}

std::string GetCurrentDir()
{
    CHAR path[MAX_PATH];
    ::GetModuleFileNameA(::GetModuleHandleA(NULL),path,MAX_PATH);
    *strrchr(path,'\\') = 0;
    return std::string(path);
}

std::wstring GetCurrentDirW()
{
    WCHAR path[MAX_PATH];
    ::GetModuleFileNameW(::GetModuleHandleW(NULL),path,MAX_PATH);
    *wcsrchr(path,L'\\') = 0;
    return std::wstring(path);
}

std::string& ModifyPath(std::string& path)
{
    if(path[0] == '/' || path[0] == '\\'){
        path = CurrentDir + path;
    }
    std::replace(path.begin(),path.end(),'/','\\');

    return path;
}

std::wstring& ModifyPath(std::wstring& path)
{
    if(path[0] == L'/' || path[0] == L'\\'){
        path = CurrentDirW + path;
    }
    std::replace(path.begin(),path.end(),L'/',L'\\');
    return path;
}

std::string GetLastErrSz()
{
    LPWSTR lpMsgBuf;
    FormatMessageW( 
        FORMAT_MESSAGE_ALLOCATE_BUFFER | 
        FORMAT_MESSAGE_FROM_SYSTEM | 
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        GetLastError(),
        0, // Default language
        (LPWSTR) &lpMsgBuf,
        0,
        NULL 
        );
    std::string Err;
	ToNarrowPath(lpMsgBuf, Err); 
    LocalFree( lpMsgBuf );
    return Err;
}

std::wstring GetLastErrSzW()
{
    LPVOID lpMsgBuf;
    FormatMessageW( 
        FORMAT_MESSAGE_ALLOCATE_BUFFER | 
        FORMAT_MESSAGE_FROM_SYSTEM | 
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        GetLastError(),
        0, // Default language
        (LPWSTR) &lpMsgBuf,
        0,
        NULL 
        );
    std::wstring Err = (LPCWSTR)lpMsgBuf;
    LocalFree(lpMsgBuf);
    return Err;
}

WorkItemWrapper::WorkItemWrapper( ScheduleProc proc_,void* content_ ) :
    proc(proc_),pContent(content_)
{

}

DWORD WINAPI WorkItemWrapperProc(LPVOID pContent)
{
    WorkItemWrapper* item = static_cast<WorkItemWrapper*>(pContent);
    ScheduleProc TempProc = item->proc;
    void* arg = item->pContent;
    delete item;
    TempProc(arg);
    return 0;
}

size_t GetPageSize()
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return std::max(si.dwPageSize,si.dwAllocationGranularity);
}

const size_t g_PageSize = GetPageSize();


Win32SequentialFile::Win32SequentialFile( const std::string& fname ) :
    _filename(fname),_hFile(NULL)
{
    _Init();
}

Win32SequentialFile::~Win32SequentialFile()
{
    _CleanUp();
}

Status Win32SequentialFile::Read( size_t n, Slice* result, char* scratch )
{
    Status sRet;
    DWORD hasRead = 0;
    if(_hFile && ReadFile(_hFile,scratch,n,&hasRead,NULL) ){
        *result = Slice(scratch,hasRead);
    } else {
        sRet = Status::IOError(_filename, Win32::GetLastErrSz() );
    }
    return sRet;
}

Status Win32SequentialFile::Skip( uint64_t n )
{
    Status sRet;
    LARGE_INTEGER Move,NowPointer;
    Move.QuadPart = n;
    if(!SetFilePointerEx(_hFile,Move,&NowPointer,FILE_CURRENT)){
        sRet = Status::IOError(_filename,Win32::GetLastErrSz());
    }
    return sRet;
}

BOOL Win32SequentialFile::isEnable()
{
    return _hFile ? TRUE : FALSE;
}

BOOL Win32SequentialFile::_Init()
{
	std::wstring path;
	ToWidePath(_filename, path);
	_hFile = CreateFileW(path.c_str(),
                         GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL,
                         OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                         NULL);
    if (_hFile == INVALID_HANDLE_VALUE)
        _hFile = NULL;
    return _hFile ? TRUE : FALSE;
}

void Win32SequentialFile::_CleanUp()
{
    if(_hFile){
        CloseHandle(_hFile);
        _hFile = NULL;
    }
}

Win32RandomAccessFile::Win32RandomAccessFile( const std::string& fname ) :
    _filename(fname),_hFile(NULL)
{
	std::wstring path;
	ToWidePath(fname, path);
    _Init( path.c_str() );
}

Win32RandomAccessFile::~Win32RandomAccessFile()
{
    _CleanUp();
}

Status Win32RandomAccessFile::Read(uint64_t offset,size_t n,Slice* result,char* scratch) const
{
    Status sRet;
    OVERLAPPED ol = {0};
    ZeroMemory(&ol,sizeof(ol));
    ol.Offset = (DWORD)offset;
    ol.OffsetHigh = (DWORD)(offset >> 32);
    DWORD hasRead = 0;
    if(!ReadFile(_hFile,scratch,n,&hasRead,&ol))
        sRet = Status::IOError(_filename,Win32::GetLastErrSz());
    else
        *result = Slice(scratch,hasRead);
    return sRet;
}

BOOL Win32RandomAccessFile::_Init( LPCWSTR path )
{
    BOOL bRet = FALSE;
    if(!_hFile)
        _hFile = ::CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS,NULL);
    if(!_hFile || _hFile == INVALID_HANDLE_VALUE )
        _hFile = NULL;
    else
        bRet = TRUE;
    return bRet;
}

BOOL Win32RandomAccessFile::isEnable()
{
    return _hFile ? TRUE : FALSE;
}

void Win32RandomAccessFile::_CleanUp()
{
 