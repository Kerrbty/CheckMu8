#include <defs.h>
#include <List.h>
#include <PV/lock.h>
#include <Http/Http.h>
#include <COneLine/COneLine.h>
#define CHECK_THREAD   8
HANDLE hGlobalFile;

typedef struct _UrlCheck 
{
    LIST_ENTRY next;
    char* name; // 直播名 
    char* url;  // 直播地址 
}UrlCheck, *PUrlCheck;

UrlCheck Lists;
CLock Listlock;
CLock fileLock;

DWORD WINAPI ThreadCheckUrl(LPVOID lparam)
{
    while( TRUE )
    {
        Listlock.lock();
        if ( _IsListEmpty(&Lists.next) )
        {
            Listlock.unlock();
            Sleep(20);
        }
        else
        {
            PLIST_ENTRY pNowList = _RemoveHeadList(&Lists.next);
            Listlock.unlock();

            PUrlCheck pOneList = CONTAINING_RECORD(pNowList, UrlCheck, next);
            if (strcmp(pOneList->url, "EndBlock") == 0)
            {
                // 列表没有数据，退出 
                break;
            }
            else
            {
                CHttp http(pOneList->url);
                const char* httpcode = http.GetReturnCodeIdA();
                if (httpcode && strstr(httpcode, "200") != NULL)
                {
                    DWORD dwBytes = 0;
                    fileLock.lock();
                    WriteFile(hGlobalFile, pOneList->name, strlen(pOneList->name), &dwBytes, NULL);
                    WriteFile(hGlobalFile, "\r\n", 2, &dwBytes, NULL);
                    WriteFile(hGlobalFile, pOneList->url, strlen(pOneList->url), &dwBytes, NULL);
                    WriteFile(hGlobalFile, "\r\n", 2, &dwBytes, NULL);
                    fileLock.unlock();
                    printf("%s\n", pOneList->url);

                    FreeMemory(pOneList->url);
                    FreeMemory(pOneList->name);
                    FreeMemory(pOneList);
                }
            }
        }
        
    }
    return 0;
}

DWORD CheckMsu8File(LPCTSTR lpFileName)
{
    HANDLE ThreadHandle[CHECK_THREAD] = {0};
    DWORD nCount = 0;
    BackFile(lpFileName);
    hGlobalFile = FileOpen(lpFileName, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ, OPEN_EXISTING);
    do 
    {
        DWORD dwBytes = 0;
        if (hGlobalFile == INVALID_HANDLE_VALUE)
            break;
        DWORD dwLen = GetFileSize(hGlobalFile, NULL);
        PBYTE pbuf = (PBYTE)AllocMemory(dwLen);
        char* extinfo = (char*)AllocMemory(dwLen);
        char* data = (char*)AllocMemory(dwLen);
        SetFilePointer(hGlobalFile, 0, NULL, FILE_BEGIN);
        ReadFile(hGlobalFile, pbuf, dwLen, &dwBytes, NULL);
        CReadLine lines(pbuf, dwLen, FALSE);

        SetFilePointer(hGlobalFile, 0, NULL, FILE_BEGIN);
        WriteFile(hGlobalFile, "#EXTM3U\r\n", strlen("#EXTM3U\r\n"), &dwBytes, NULL);
        _InitializeListHead(&Lists.next);

        DWORD dwReadLen = dwLen;
        while( lines.getLine(data, &dwReadLen) )
        {
            if (memicmp(data, "#EXTINF:", 8) == 0)
            {
                strcpy(extinfo, data);
            }
            else
            {
                // 挂入队列 
                PUrlCheck plist = (PUrlCheck)AllocMemory(sizeof(UrlCheck));
                plist->name = (char*)AllocMemory(strlen(extinfo)+1);
                strcpy(plist->name, extinfo);
                plist->url = (char*)AllocMemory(strlen(data)+1);
                strcpy(plist->url, data);
                Listlock.lock();
                _InsertTailList(&Lists.next, &plist->next);
                Listlock.unlock();
            }
            dwReadLen = dwLen;
        }
        // 插入线程终止 
        for (int i=0; i<CHECK_THREAD; i++)
        {
            PUrlCheck plist = (PUrlCheck)AllocMemory(sizeof(UrlCheck));
            plist->url = (char*)AllocMemory(MAX_PATH);
            strcpy(plist->url, "EndBlock");
            Listlock.lock();
            _InsertTailList(&Lists.next, &plist->next);
            Listlock.unlock();
        }

        // 启动检测线程
        for (int i=0; i<CHECK_THREAD; i++)
        {
            ThreadHandle[i] = CreateThread(NULL, 0, ThreadCheckUrl, NULL, 0, NULL);
        }

        // 等待 
        WaitForMultipleObjects(CHECK_THREAD, ThreadHandle, TRUE, INFINITE);

        // 退出 
        for (int i=0; i<CHECK_THREAD; i++)
        {
            CloseHandle(ThreadHandle[i]);
        }
        SetEndOfFile(hGlobalFile);

        FreeMemory(data);
        FreeMemory(extinfo);
        FreeMemory(pbuf);
    } while (FALSE);
    CloseHandle(hGlobalFile);
    return nCount;
}

int _tmain(INT argc, TCHAR **argv)
{
    for (int i=1; i<argc; i++)
    {
        CheckMsu8File(argv[i]);
    }
    return 0;
}