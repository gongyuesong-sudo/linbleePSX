#include <windows.h>
#include <stdio.h>
typedef NTSTATUS(WINAPI*QNQ)(HANDLE,INT,PVOID,ULONG,PULONG);
int main(void){
  unsigned teb=0; __asm__("movl %%fs:0x18,%0":"=r"(teb));
  unsigned peb=0; if(teb) peb=*(unsigned*)(teb+0x30);
  unsigned char bd=0; DWORD ngf=0xFFFFFFFF, mut=0;
  if(peb){ bd=*(unsigned char*)(peb+2); ngf=*(DWORD*)(peb+0x68); mut=*(DWORD*)(peb+0x14); }
  printf("TEB=%08x PEB=%08x BeingDebugged=%d NtGlobalFlag=%08x Mutant=%08x\n",teb,peb,bd,ngf,mut);
  BOOL present=0; BOOL r=CheckRemoteDebuggerPresent(GetCurrentProcess(),&present);
  printf("CheckRemote fnret=%d *present=%d\n", r, present);
  QNQ q=(QNQ)GetProcAddress(GetModuleHandleA("ntdll"),"NtQueryInformationProcess");
  if(q){ ULONG br=0; ULONGLONG port=~0ULL; int df=-1;
    printf("DebugPort  st=%08x val=%I64u\n",(unsigned)q((HANDLE)-1,7,&port,8,&br),port);
    printf("DebugFlags st=%08x val=%d\n",(unsigned)q((HANDLE)-1,0x1f,&df,4,&br),df);
    port=~0ULL;
    printf("DebugObjH  st=%08x val=%I64u\n",(unsigned)q((HANDLE)-1,0x1e,&port,8,&br),port);
  } else printf("no NQIP\n");
  return 0;
}
