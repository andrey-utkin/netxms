#include <nms_common.h>
#include <nms_util.h>
#include <testtools.h>

#include "../../src/agent/subagents/linux/cpu.h"

static Collector *collector = nullptr;
// m_cpuUsageMutex must be held to access `collector`, thread and its internals
static Mutex m_cpuUsageMutex(MutexType::FAST);

const int collectionPeriodMs = 10;
/**
 * CPU usage collector thread
 */
static void CpuUsageCollectorThread()
{
   //nxlog_debug_tag(DEBUG_TAG, 2, _T("CPU usage collector thread started"));

   m_cpuUsageMutex.lock();
   while(collector->m_stopThread == false)
   {
      collector->Collect();
      m_cpuUsageMutex.unlock();
      ThreadSleepMs(collectionPeriodMs);
      m_cpuUsageMutex.lock();
   }
   m_cpuUsageMutex.unlock();
   //nxlog_debug_tag(DEBUG_TAG, 2, _T("CPU usage collector thread stopped"));
}

/**
 * Start CPU usage collector
 */
void StartCpuUsageCollector()
{
   m_cpuUsageMutex.lock();
   if (collector != nullptr)
   {
      //nxlog_write(NXLOG_ERROR, _T("CPU Usage Collector extraneous initialization detected!"));
   }
   assert(collector == nullptr);
   collector = new Collector();

   // start collector
   collector->m_thread = ThreadCreateEx(CpuUsageCollectorThread);
   m_cpuUsageMutex.unlock();
}

/**
 * Shutdown CPU usage collector
 */
void ShutdownCpuUsageCollector()
{
   m_cpuUsageMutex.lock();
   collector->m_stopThread = true;
   m_cpuUsageMutex.unlock();
   ThreadJoin(collector->m_thread);
   m_cpuUsageMutex.lock();
   delete collector;
   collector = nullptr;
   m_cpuUsageMutex.unlock();
}
#if 0
LONG H_CpuUsage(const TCHAR *pszParam, const TCHAR *pArg, TCHAR *pValue, AbstractCommSession *session)
{
        int count;
        switch(CPU_USAGE_PARAM_INTERVAL(pArg))
        {
                case INTERVAL_5MIN:
                        count = 5 * 60;
                        break;
                case INTERVAL_15MIN:
                        count = 15 * 60;
                        break;
                default:
                        count = 60;
                        break;
        }
        enum CpuUsageSource source = (enum CpuUsageSource)CPU_USAGE_PARAM_SOURCE(pArg);
        m_cpuUsageMutex.lock();
        float usage = collector->GetTotalUsage(source, count);
        ret_double(pValue, usage);
        m_cpuUsageMutex.unlock();
        return SYSINFO_RC_SUCCESS;
}

LONG H_CpuUsageEx(const TCHAR *pszParam, const TCHAR *pArg, TCHAR *pValue, AbstractCommSession *session)
{
   LONG ret;
   TCHAR buffer[256] = {0,}, *eptr;
   nxlog_debug_tag(DEBUG_TAG, 4, _T("pszParam: '%s'"), pszParam);
   if (!AgentGetParameterArg(pszParam, 1, buffer, 256))
      return SYSINFO_RC_UNSUPPORTED;

   int cpu = _tcstol(buffer, &eptr, 0);
   if ((*eptr != 0) || (cpu < 0))
      return SYSINFO_RC_UNSUPPORTED;

   int count;
   switch(CPU_USAGE_PARAM_INTERVAL(pArg))
   {
      case INTERVAL_5MIN:
         count = 5 * 60;
         break;
      case INTERVAL_15MIN:
         count = 15 * 60;
         break;
      default:
         count = 60;
         break;
   }
   enum CpuUsageSource source = (enum CpuUsageSource)CPU_USAGE_PARAM_SOURCE(pArg);
   m_cpuUsageMutex.lock();
   nxlog_debug_tag(DEBUG_TAG, 4, _T("collector: %p"), collector);
   nxlog_debug_tag(DEBUG_TAG, 4, _T("collector: %p, m_perCore %p"), collector, &collector->m_perCore);
   nxlog_debug_tag(DEBUG_TAG, 4, _T("collector: %p, m_perCore %p, size %d"), collector, &collector->m_perCore, collector->m_perCore.size());
   if (cpu >= collector->m_perCore.size())
   {
      ret = SYSINFO_RC_UNSUPPORTED;
   }
   else
   {
      float usage = collector->GetCoreUsage(source, cpu, count);
      ret_double(pValue, usage);
      ret = SYSINFO_RC_SUCCESS;
   }
   m_cpuUsageMutex.unlock();
   return ret;
}

/**
 * Handler for System.CPU.Count parameter
 */
LONG H_CpuCount(const TCHAR *pszParam, const TCHAR *pArg, TCHAR *pValue, AbstractCommSession *session)
{
   m_cpuUsageMutex.lock();
   ret_uint(pValue, collector->m_perCore.size());
   m_cpuUsageMutex.unlock();
   return SYSINFO_RC_SUCCESS;
}
#endif


static void ServeAllMetrics()
{
   m_cpuUsageMutex.lock();
   int nbCoresActual = collector->m_perCore.size();
   for (enum CpuUsageSource source = (enum CpuUsageSource)0; source < CPU_USAGE_NB_SOURCES; source = (enum CpuUsageSource)((int)source + 1))
   {
      for (enum CpuUsageInterval interval = INTERVAL_1MIN; interval <= INTERVAL_15MIN; interval = (enum CpuUsageInterval)((int)interval + 1))
      {
         int count;
         switch(interval)
         {
            case INTERVAL_5MIN: count = 5 * 60; break;
            case INTERVAL_15MIN: count = 15 * 60; break;
            default: count = 60; break;
         }
         
         for (int coreIndex = 0; coreIndex < nbCoresActual; coreIndex++)
         {
            float usage = collector->GetCoreUsage(source, coreIndex, count);
            (void)usage;
         }
         float usage = collector->GetTotalUsage(source, count);
         (void)usage;
      }
   }
   m_cpuUsageMutex.unlock();
}

void TestCpu()
{
   StartTest(_T("CPU stats collector - single threaded work"));
   assert(collector == nullptr);
   collector = new Collector();

   // We have to let it populate the tables with at least one delta value, otherwise it assers
   // For this, two readings are necessary
   collector->Collect();

   for (int i = 0; i < CPU_USAGE_SLOTS * 2; i++)
   {
      collector->Collect();
      ServeAllMetrics();
   }
   delete(collector);
   collector = nullptr;
   EndTest();

   StartTest(_T("CPU stats collector - multi-threaded work"));
   StartCpuUsageCollector();
   ThreadSleepMs(2000);
   INT64 start = GetCurrentTimeMs();
   while (GetCurrentTimeMs() - start < CPU_USAGE_SLOTS * collectionPeriodMs * 2)
   {
      ServeAllMetrics();
      //ThreadSleepMs(collectionPeriodMs/2);
   }
   ShutdownCpuUsageCollector();
   delete(collector);
   EndTest();
   // torture test
   // sense timeout, perhaps create consumer thread and here do ThreadSleepMs(1000);

}
