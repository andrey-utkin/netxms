/* 
** NetXMS subagent for GNU/Linux
** Copyright (C) 2004-2022 Raden Solutions
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
**/

#include "linux_subagent.h"

#define CPU_USAGE_SLOTS			900 // 60 sec * 15 min => 900 sec

/**
 * Almost RingBuffer.
 * But:
 * - not byte-based (float-based here)
 * - fixed in size (no growth requirements)
 * - data always fed in single pieces
 */
class MeasurementsTable
{
public:
   float m_data[CPU_USAGE_SLOTS];
   const uint32_t m_allocated = CPU_USAGE_SLOTS; // how big is the array? in elements, not bytes
   uint32_t m_size; // how many are stored? in elements, not bytes
   uint32_t m_writePos; // where to write next element? in elements, not bytes
   float GetAverage(uint32_t nbLastItems);
   void Reset();
   void Update(float measurement);
   MeasurementsTable();
};

MeasurementsTable::MeasurementsTable():
   m_size(0),
   m_writePos(0)
{
   for (int i = 0; i < CPU_USAGE_SLOTS; i++)
   {
      m_data[i] = 0;
   }
}

float MeasurementsTable::GetAverage(uint32_t nbLastItems)
{
   float total = 0.0;
   uint32_t nbElem = std::min(m_size, nbLastItems);

   assert(nbElem != 0);
   if (nbElem == 0)
   {
      return 0;
   }

   assert(m_size <= m_allocated);
   assert(m_writePos < m_allocated);

   nxlog_debug_tag(DEBUG_TAG, 4, _T("Getting average over nbElem=%u, buffer has m_size=%u, m_writePos=%u"), nbElem, m_size, m_writePos);

   for (uint32_t i = 0; i < nbElem; i++)
   {
      uint32_t offset = (m_writePos - i - 1) % m_allocated;

      total += m_data[offset];
      nxlog_debug_tag(DEBUG_TAG, 4, _T("Getting element by offset=%u"), offset);
   }
   return total / nbElem;
}

void MeasurementsTable::Reset()
{
   m_size = 0;
   m_writePos = 0;
}

void MeasurementsTable::Update(float measurement)
{
   assert(m_size <= m_allocated);
   assert(m_writePos < m_allocated);
   auto debugPrevSize = m_size;

   nxlog_debug_tag(DEBUG_TAG, 4, _T("Putting element by offset=%u"), m_writePos);
   m_data[m_writePos] = measurement;
   m_writePos = (m_writePos + 1) % m_allocated;
   m_size = std::min(m_size + 1, m_allocated);

   assert(m_size <= m_allocated);
   assert(m_writePos < m_allocated);

   if (debugPrevSize == m_allocated)
   {
      assert(m_size == m_allocated);
   }
   else
   {
      assert(m_size == 1 + debugPrevSize);
   }
}


class CpuStats
{
public:
   MeasurementsTable m_tables[CPU_USAGE_NB_SOURCES];
   void Update(uint64_t measurements[CPU_USAGE_NB_SOURCES]);
   bool IsOn();
   void SetOff();
   CpuStats();
private:
   bool m_on;
   bool m_havePrevMeasurements;
   uint64_t m_prevMeasurements[CPU_USAGE_NB_SOURCES];
   static inline uint64_t Delta(uint64_t x, uint64_t y);
};

CpuStats::CpuStats():
   m_on(false),
   m_havePrevMeasurements(false)
{
   for (int i = 0; i < CPU_USAGE_NB_SOURCES; i++)
   {
      new (&m_tables[i]) MeasurementsTable();
   }
}

void CpuStats::SetOff()
{
   for (int i = 0; i < CPU_USAGE_NB_SOURCES; i++)
   {
      m_tables[i].Reset();
   }
   m_on = false;
   m_havePrevMeasurements = false;
}


class Collector
{
public:
   // m_cpuUsageMutex must be held to access anything
   bool m_stopThread;
   THREAD m_thread;

   CpuStats m_total;
   std::vector<CpuStats> m_perCore;
   uint64_t m_cpuInterrupts;
   uint64_t m_cpuContextSwitches;
   void Collect(void);
   Collector();
   float GetCoreUsage(enum CpuUsageSource source, int coreIndex, int nbLastItems);
   float GetTotalUsage(enum CpuUsageSource source, int nbLastItems);
};

static Collector *collector = nullptr;
// m_cpuUsageMutex must be held to access `collector`, thread and its internals
static Mutex m_cpuUsageMutex(MutexType::FAST);

Collector::Collector():
   m_stopThread(false),
   m_thread(INVALID_THREAD_HANDLE),
   m_total(),
   m_perCore(0),
   m_cpuInterrupts(0),
   m_cpuContextSwitches(0)
{
}

/**
 * CPU usage data collection
 *
 * Must be called with the mutex held.
 */
void Collector::Collect()
{
   FILE *hStat = fopen("/proc/stat", "r");
   if (hStat == nullptr)
   {
      nxlog_debug_tag(DEBUG_TAG, 4, _T("Cannot open /proc/stat"));
      return;
   }

   char buffer[1024];
   std::vector<bool> coreReported(this->m_perCore.size());

   // scan for all CPUs
   while(true)
   {
      if (fgets(buffer, sizeof(buffer), hStat) == NULL)
         break;

      int ret;
      if (buffer[0] == 'c' && buffer[1] == 'p' && buffer[2] == 'u')
      {
         uint64_t user, nice, system, idle;
         uint64_t iowait = 0, irq = 0, softirq = 0; // 2.6
         uint64_t steal = 0; // 2.6.11
         uint64_t guest = 0; // 2.6.24
         if (buffer[3] == ' ')
         {
            // "cpu ..." - Overall across all cores
            ret = sscanf(buffer, "cpu " UINT64_FMTA " " UINT64_FMTA " " UINT64_FMTA " " UINT64_FMTA " " UINT64_FMTA " " UINT64_FMTA " " UINT64_FMTA " " UINT64_FMTA " " UINT64_FMTA,
                  &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal, &guest);
            if (ret == 9) {
               uint64_t measurements[CPU_USAGE_NB_SOURCES] = {0, user, nice, system, idle, iowait, irq, softirq, steal, guest};
               m_total.Update(measurements);
            }
         }
         else
         {
            uint32_t cpuIndex = 0;
            ret = sscanf(buffer, "cpu%u " UINT64_FMTA " " UINT64_FMTA " " UINT64_FMTA " " UINT64_FMTA " " UINT64_FMTA " " UINT64_FMTA " " UINT64_FMTA " " UINT64_FMTA " " UINT64_FMTA,
                  &cpuIndex, &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal, &guest);
            if (ret == 10) {
               if (m_perCore.size() < cpuIndex + 1)
               {
                  nxlog_debug_tag(DEBUG_TAG, 4, _T("Growing cores vector from %u to %u"), m_perCore.size(), cpuIndex + 1);
                  m_perCore.resize(cpuIndex + 1); CpuStats &thisCore = m_perCore.at(cpuIndex); assert(thisCore.IsOn() == false); assert(thisCore.m_tables[0].m_size == 0);
                  coreReported.resize(cpuIndex + 1);
               }
               CpuStats &thisCore = m_perCore.at(cpuIndex);
               uint64_t measurements[CPU_USAGE_NB_SOURCES] = {0, user, nice, system, idle, iowait, irq, softirq, steal, guest};
               thisCore.Update(measurements);
               coreReported[cpuIndex] = true;
            }
         }
      }
      else if (buffer[0] == 'i' && buffer[1] == 'n' && buffer[2] == 't' && buffer[3] == 'r')
      {
         ret = sscanf(buffer, "intr " UINT64_FMTA, &collector->m_cpuInterrupts);
         assert(ret == 1); // between us and kernel we should always get this right
      }
      else if (buffer[0] == 'c' && buffer[1] == 't' && buffer[2] == 'x' && buffer[3] == 't')
      {
         ret = sscanf(buffer, "ctxt " UINT64_FMTA, &collector->m_cpuContextSwitches);
         assert(ret == 1); // between us and kernel we should always get this right
      }
      else
      {
         continue;
      }

   } // while(true) file traversal
   fclose(hStat);
   for (uint32_t cpuIndex = 0; cpuIndex < coreReported.size(); cpuIndex++)
   {
      if (!coreReported[cpuIndex] && m_perCore[cpuIndex].IsOn())
      {
         nxlog_debug_tag(DEBUG_TAG, 4, _T("Core %u was not reported this time"), m_perCore.size(), cpuIndex + 1);
         m_perCore[cpuIndex].SetOff();
      }
   }
}

bool CpuStats::IsOn()
{
   return m_on; // suspected uninitalized??? wtf
}

inline uint64_t CpuStats::Delta(uint64_t x, uint64_t y)
{
   return (x > y) ? (x - y) : 0;
}

void CpuStats::Update(uint64_t measurements[CPU_USAGE_NB_SOURCES])
{
   uint64_t deltas[CPU_USAGE_NB_SOURCES] = {0,};
   uint64_t totalDelta = 0;

   if (m_havePrevMeasurements) {
      for (int i = 1 /* skip CPU_USAGE_OVERAL */; i < CPU_USAGE_NB_SOURCES; i++)
      {
         uint64_t delta = Delta(measurements[i], m_prevMeasurements[i]);
         totalDelta += delta;
         deltas[i] = delta;
      }

      float onePercent = (float)totalDelta / 100.0; // 1% of total
      if (onePercent == 0)
      {
         onePercent = 1; // TODO: why 1?
      }

      /* update detailed stats */
      for (int i = 1 /* skip CPU_USAGE_OVERAL */; i < CPU_USAGE_NB_SOURCES; i++)
      {
         uint64_t delta = deltas[i];
         m_tables[i].Update(delta == 0 ? 0 : (float)delta / onePercent);
      }
      /* update overal cpu usage */
      m_tables[CPU_USAGE_OVERAL].Update(totalDelta == 0 ? 0 : 100.0 - (float)deltas[CPU_USAGE_IDLE] / onePercent);

   }
   for (int i = 1 /* skip CPU_USAGE_OVERAL */; i < CPU_USAGE_NB_SOURCES; i++)
   {
      m_prevMeasurements[i] = measurements[i];
   }
   m_havePrevMeasurements = true;
   m_on = true;
}

/**
 * CPU usage collector thread
 */
static void CpuUsageCollectorThread()
{
   nxlog_debug_tag(DEBUG_TAG, 2, _T("CPU usage collector thread started"));

   m_cpuUsageMutex.lock();
   while(collector->m_stopThread == false)
   {
      collector->Collect();
      m_cpuUsageMutex.unlock();
      ThreadSleepMs(1000); // sleep 1 second
      m_cpuUsageMutex.lock();
   }
   m_cpuUsageMutex.unlock();
   nxlog_debug_tag(DEBUG_TAG, 2, _T("CPU usage collector thread stopped"));
}

/**
 * Start CPU usage collector
 */
void StartCpuUsageCollector()
{
   m_cpuUsageMutex.lock();
   if (collector != nullptr)
   {
      nxlog_write(NXLOG_ERROR, _T("CPU Usage Collector extraneous initialization detected!"));
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

float Collector::GetTotalUsage(enum CpuUsageSource source, int nbLastItems)
{
      return m_total.m_tables[source].GetAverage(nbLastItems);
}

/**
 * @param coreIndex 0-based core index
 */
float Collector::GetCoreUsage(enum CpuUsageSource source, int coreIndex, int nbLastItems)
{
   if (coreIndex > 100)
   {
      abort();
   }
   if (m_perCore.size() > 100)
   {
      abort();
   }
   if (coreIndex >= m_perCore.size())
   {
      return 0;
   }

   CpuStats &core = m_perCore[coreIndex];

   // If core wasn't reported, or no delta-based samples yet, we have nothing to average.
   if (!core.IsOn() || core.m_tables[source].m_size == 0)
   {
      return 0;
   }
   return core.m_tables[source].GetAverage(nbLastItems);
}

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

/**
 * CPU info structure
 */
struct CPU_INFO
{
   int id;
   int coreId;
   int physicalId;
   char model[64];
   INT64 frequency;
   int cacheSize;
};

/**
 * Read /proc/cpuinfo
 */
static int ReadCpuInfo(CPU_INFO *info, int size)
{
   FILE *f = fopen("/proc/cpuinfo", "r");
   if (f == nullptr)
   {
      nxlog_debug_tag(DEBUG_TAG, 4, _T("Cannot open /proc/cpuinfo"));
      return -1;
   }

   int count = -1;
   char buffer[256];
   while(!feof(f))
   {
      if (fgets(buffer, sizeof(buffer), f) == nullptr)
         break;
      char *s = strchr(buffer, '\n');
      if (s != nullptr)
         *s = 0;

      s = strchr(buffer, ':');
      if (s == nullptr)
         continue;

      *s = 0;
      s++;
      TrimA(buffer);
      TrimA(s);

      if (!strcmp(buffer, "processor"))
      {
         count++;
         memset(&info[count], 0, sizeof(CPU_INFO));
         info[count].id = (int)strtol(s, nullptr, 10);
         continue;
      }

      if (count == -1)
         continue;

      if (!strcmp(buffer, "model name"))
      {
         strncpy(info[count].model, s, 63);
      }
      else if (!strcmp(buffer, "cpu MHz"))
      {
         char *eptr;
         info[count].frequency = strtoll(s, &eptr, 10) * _LL(1000);
         if (*eptr == '.')
         {
            eptr[4] = 0;
            info[count].frequency += strtoll(eptr + 1, nullptr, 10);
         }
      }
      else if (!strcmp(buffer, "cache size"))
      {
         info[count].cacheSize = (int)strtol(s, nullptr, 10);
      }
      else if (!strcmp(buffer, "physical id"))
      {
         info[count].physicalId = (int)strtol(s, nullptr, 10);
      }
      else if (!strcmp(buffer, "core id"))
      {
         info[count].coreId = (int)strtol(s, nullptr, 10);
      }
   }

   fclose(f);
   return count + 1;
}

/**
 * Handler for CPU info parameters
 */
LONG H_CpuInfo(const TCHAR *param, const TCHAR *arg, TCHAR *value, AbstractCommSession *session)
{
   CPU_INFO cpuInfo[256];
   int count = ReadCpuInfo(cpuInfo, 256);
   if (count <= 0)
      return SYSINFO_RC_ERROR;

   TCHAR buffer[32];
   AgentGetParameterArg(param, 1, buffer, 32);
   int cpuId = (int)_tcstol(buffer, NULL, 0);

   CPU_INFO *cpu = NULL;
   for(int i = 0; i < count; i++)
   {
      if (cpuInfo[i].id == cpuId)
      {
         cpu = &cpuInfo[i];
         break;
      }
   }
   if (cpu == NULL)
      return SYSINFO_RC_NO_SUCH_INSTANCE;

   switch(*arg)
   {
      case 'C':   // Core ID
         ret_int(value, cpu->coreId);
         break;
      case 'F':   // Frequency
         _sntprintf(value, MAX_RESULT_LENGTH, _T("%d.%03d"),
               static_cast<int>(cpu->frequency / 1000), static_cast<int>(cpu->frequency % 1000));
         break;
      case 'M':   // Model
         ret_mbstring(value, cpu->model);
         break;
      case 'P':   // Physical ID
         ret_int(value, cpu->physicalId);
         break;
      case 'S':   // Cache size
         ret_int(value, cpu->cacheSize);
         break;
      default:
         return SYSINFO_RC_UNSUPPORTED;
   }

   return SYSINFO_RC_SUCCESS;
}

/*
 * Handler for CPU Context Switch parameter
 */
LONG H_CpuCswitch(const TCHAR *param, const TCHAR *arg, TCHAR *value, AbstractCommSession *session)
{
   m_cpuUsageMutex.lock();
   ret_uint(value, collector->m_cpuContextSwitches);
   m_cpuUsageMutex.unlock();
   return SYSINFO_RC_SUCCESS;
}

/*
 * Handler for CPU Interrupts parameter
 */
LONG H_CpuInterrupts(const TCHAR *param, const TCHAR *arg, TCHAR *value, AbstractCommSession *session)
{
   m_cpuUsageMutex.lock();
   ret_uint(value, collector->m_cpuInterrupts);
   m_cpuUsageMutex.unlock();
   return SYSINFO_RC_SUCCESS;
}
