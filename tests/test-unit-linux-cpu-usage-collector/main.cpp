#ifdef _WIN32
#define _CRT_NONSTDC_NO_WARNINGS
#endif

#include <nms_common.h>
#include <nms_util.h>
#include <nxcpapi.h>
#include <nxproc.h>
#include <testtools.h>
#include <netxms-version.h>

NETXMS_EXECUTABLE_HEADER(test-libnetxms)

void TestCpu();

/**
 * Debug writer for logger
 */
static void DebugWriter(const TCHAR *tag, const TCHAR *format, va_list args)
{
   if (tag != NULL)
      _tprintf(_T("[DEBUG/%-20s] "), tag);
   else
      _tprintf(_T("[DEBUG%-21s] "), _T(""));
   _vtprintf(format, args);
   _fputtc(_T('\n'), stdout);
}

/**
 * main()
 */
int main(int argc, char *argv[])
{
   bool debug = false;

   InitNetXMSProcess(true);
   if (argc > 1)
   {
      if (!strcmp(argv[1], "@proc"))
      {
         TestProcessExecutorWorker();
         return 0;
      }
      else if (!strcmp(argv[1], "@subproc"))
      {
         if ((argc > 2) && !strcmp(argv[2], "-debug"))
         {
            nxlog_open(_T("subprocess.log"), 0);
            nxlog_set_debug_level(9);
         }
         SubProcessMain(argc, argv, TestSubProcessRequestHandler);
         return 0;
      }
      else if (!strcmp(argv[1], "-debug"))
      {
         nxlog_set_debug_writer(DebugWriter);
         debug = true;
      }
   }

   TestCpu();

   InitiateProcessShutdown();

   return 0;
}
