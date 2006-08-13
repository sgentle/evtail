
#include <windows.h>
#include <stdio.h>
#include <strings.h>
#include <time.h>

#define BUFFER_SIZE 1024*4
#define RDATABUFFER 1024        //registry data buffer size
#define MFMSGBUFFER 1024        //MessaeFile message buffer size

/* Prototypes */
void PrintError(void);
char *GetFriendlyEventType(WORD wEventType);
int GetFormattedEventMessage(char lpsSourceName[], 
    EVENTLOGRECORD *pevlr, char sReturnString[]);
int PointlessCrudToActualArray(EVENTLOGRECORD *pevlr, 
    DWORD_PTR ActualArray[]);

/* Globals */
char sLogName[255] = "Application";

int main(int argc, char *argv[])
{
    HANDLE h;
    EVENTLOGRECORD *pevlr; 
    BYTE bBuffer[BUFFER_SIZE]; 
    DWORD dwRead, dwNeeded, dwThisRecord, dwNumRecords; 
    int iEntriesToRead = 5;
    
    if (argc > 1) { strcpy(sLogName,argv[1]); }
    
    // Open the Application event log. 
 
    h = OpenEventLog( NULL,    // use local computer
             sLogName);   // source name
    
    if (h == NULL) 
    {
        //printf("Could not open the Application event log."); 
        PrintError();
        return 0;
    }
 
    pevlr = (EVENTLOGRECORD *) &bBuffer; 
 

    GetOldestEventLogRecord(h, &dwThisRecord);  
    GetNumberOfEventLogRecords(h, &dwNumRecords);

    /*
    printf("There are %d entries in this event log, starting at %d...\n", 
        dwNumRecords, dwThisRecord);
    */
    while (1) {
        if (ReadEventLog(h,                // event log handle 
                    EVENTLOG_FORWARDS_READ |  // reads forward 
                    EVENTLOG_SEEK_READ, // seek read 
                    dwThisRecord + dwNumRecords - iEntriesToRead,
                    pevlr,        // pointer to buffer 
                    BUFFER_SIZE,  // size of buffer 
                    &dwRead,      // number of bytes read 
                    &dwNeeded))   // bytes in next record 
        {
            while (dwRead > 0) 
            { 
                // Print the record number, event identifier, type, 
                // and source name. 
    
                LPSTR lpsSourceName = 
                    (LPSTR) ((LPBYTE) pevlr + sizeof(EVENTLOGRECORD));
    
                LPSTR lpsComputerName = 
                    (LPSTR) ((LPBYTE) pevlr + sizeof(EVENTLOGRECORD) + 
                        strlen(lpsSourceName) + 1);
                
                char sEventMessage[RDATABUFFER];
                GetFormattedEventMessage(lpsSourceName, pevlr, sEventMessage);
                
                struct tm tmEventTime = *localtime(&pevlr->TimeGenerated);
                char sEventTime[255];
                
                strftime(sEventTime, 255, "%Y-%m-%d %H:%M:%S", &tmEventTime);

                printf("%s [%s] -> %s\n%s: %s \n\n",
                    sEventTime,
                    lpsComputerName,
                    lpsSourceName,
                    GetFriendlyEventType(pevlr->EventType),
                    sEventMessage);
               
                dwRead -= pevlr->Length; 
                pevlr = (EVENTLOGRECORD *) 
                    ((LPBYTE) pevlr + pevlr->Length); 
            } 
     
            pevlr = (EVENTLOGRECORD *) &bBuffer; 
        } 
        else 
        {
            PrintError();
        }    
        
        //Going into tail mode - read one entry at a time
        iEntriesToRead = 1;
        
        //Set an event object
        HANDLE hEvent;
        hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (hEvent == NULL) { PrintError(); }
        NotifyChangeEventLog(h, hEvent);
        
        //Wait for trigger
        WaitForSingleObject(hEvent, INFINITE);
        ResetEvent (hEvent);
        
    }    
    CloseEventLog(h); 
}



int GetFormattedEventMessage(char lpsSourceName[], 
    EVENTLOGRECORD *pevlr, char sReturnString[])
{
    LONG lRet;
    HKEY hKey;
    
    DWORD dwMFLen = RDATABUFFER; //Registry data buffer for unexpanded
    char sMessageFile[dwMFLen];  //Message File name
    
    DWORD dwRMFLen = RDATABUFFER;    //Registry data buffer for expaned
    char sRealMessageFile[dwRMFLen]; //Message File name
    
    DWORD dwMFMLen = MFMSGBUFFER; //Resource data buffer for message string
    char sMFMessage[dwMFMLen];
    
    DWORD_PTR dwpRepStrings[99]; //Pointer array of replacement strings
    
    HANDLE hMFHandle;
    
    int iKTOLen = 49 + strlen(sLogName) + strlen(lpsSourceName);
    char sKeyToOpen[iKTOLen];

    sprintf(sKeyToOpen, 
        "SYSTEM\\CurrentControlSet\\Services\\EventLog\\%s\\%s",
        sLogName, lpsSourceName);
       
    lRet = RegOpenKeyEx(HKEY_LOCAL_MACHINE,
        sKeyToOpen,
        0,
        KEY_QUERY_VALUE,
        &hKey);
    
    if (lRet != ERROR_SUCCESS) {
        PrintError();
        strcpy(sReturnString, "Could not access Message String Key");
    }

    //printf("\n%s\\EventMessageFile\n", sKeyToOpen);

    lRet = RegQueryValueEx(hKey, "EventMessageFile",
        NULL, NULL, sMessageFile, &dwMFLen);

    if (lRet != ERROR_SUCCESS) {
        PrintError();
        strcpy(sReturnString, "Could not access Message String Value");
    }
        
    //Should possibly check here to ensure return value is REG_EXPAND_SZ 
                
    RegCloseKey(hKey);  //free registry handle
    
    ExpandEnvironmentStrings(sMessageFile, sRealMessageFile, dwRMFLen);
   
    hMFHandle = LoadLibraryEx(sRealMessageFile, 0, LOAD_LIBRARY_AS_DATAFILE);
    
    if (hMFHandle == NULL) {
        PrintError();
        strcpy(sReturnString, "Could not load Message String Module");
    }

    PointlessCrudToActualArray(pevlr, dwpRepStrings);
    
    lRet = FormatMessage(
        FORMAT_MESSAGE_FROM_HMODULE |   //Flags: From module (MF library)
        FORMAT_MESSAGE_FROM_SYSTEM |    //  or system
        FORMAT_MESSAGE_ARGUMENT_ARRAY,  //  Should make EVENTLOGLIBRARY work
        hMFHandle,                      //Source: Handle to MF library
        pevlr->EventID,                 //Message ID: Event ID
        0,                              //Language ID: Auto
        sMFMessage,                     //Buffer: pointer to MFM string
        dwMFMLen,                       //Size: length of MFM string
        (va_list *) dwpRepStrings);     //Arguments: Replacement strings
        
    if (lRet == 0) {
        PrintError();
        strcpy(sReturnString, "Could not read Message String resource table");
    }

    FreeLibrary(hMFHandle);
    
    strcpy(sReturnString, sMFMessage);
    return 0;
}    
int PointlessCrudToActualArray(EVENTLOGRECORD *pevlr, 
    DWORD_PTR ActualArray[]) 
{
    int l;
    //DWORD c = (DWORD) &pevlr + pevlr->StringOffset - 1;
    LPBYTE c = ((LPBYTE) pevlr + pevlr->StringOffset);
    //printf ("%d - %d\n", pevlr->StringOffset, sizeof(EVENTLOGRECORD));
    //printf("\t (Contains %d strings)\n", pevlr->NumStrings);
    for (l = 0; l < pevlr->NumStrings; l++)
    {
        ActualArray[l] = (int) c;
        //printf("\tReplacement string at %p: %s\n", c, (char *) c);
        c += strlen((char *) c) + 1;
    }    
    return 0;
}    
    
void PrintError(void) 
{
        DWORD dwError = GetLastError();
        LPVOID lpMsg;
        
        FormatMessage(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | 
            FORMAT_MESSAGE_FROM_SYSTEM,
            NULL,
            dwError,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPTSTR) &lpMsg,
            0, NULL );
        
        printf("Error %d: %s\n", (int) dwError, (char *) lpMsg);

}    

char *GetFriendlyEventType(WORD wEventType)
{
    switch(wEventType)
    {
        case EVENTLOG_ERROR_TYPE:
            return "Error";
            break;
        case EVENTLOG_WARNING_TYPE:
            return "Warning";
            break;
        case EVENTLOG_INFORMATION_TYPE:
            return "Information";
            break;
        case EVENTLOG_AUDIT_SUCCESS:
            return "Audit Success";
            break;
        case EVENTLOG_AUDIT_FAILURE:
            return "Audit Failure";
            break;
        default:
            return "Unknown Event";
            break;
    }
}
