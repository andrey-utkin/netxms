/*
** NetXMS - Network Management System
** Copyright (C) 2003-2021 Raden Solutions
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
** File: businessservicecheck.cpp
**
**/

#include "nxcore.h"

#define DEBUG_TAG _T("business.service.check")

/**
 * Create empty business service check object
 */
BusinessServiceCheck::BusinessServiceCheck(uint32_t serviceId) : m_description(_T("Unnamed"))
{
   m_id = CreateUniqueId(IDG_BUSINESS_SERVICE_CHECK);
   m_type = BusinessServiceCheckType::OBJECT;
   m_status = STATUS_NORMAL;
   m_script = nullptr;
   m_compiledScript = nullptr;
   m_reason[0] = 0;
   m_relatedObject = 0;
   m_relatedDCI = 0;
   m_currentTicket = 0;
   m_serviceId = serviceId;
   m_statusThreshold = 0;
   m_mutex = MutexCreateFast();
}

/**
 * Create new business service check
 */
BusinessServiceCheck::BusinessServiceCheck(uint32_t serviceId, BusinessServiceCheckType type, uint32_t relatedObject, uint32_t relatedDCI, const TCHAR* description, int threshhold) :
         m_description((description != nullptr) ? description : _T("Unnamed"))
{
   m_id = CreateUniqueId(IDG_BUSINESS_SERVICE_CHECK);
   m_type = type;
   m_status = STATUS_NORMAL;
   m_script = nullptr;
   m_compiledScript = nullptr;
   m_reason[0] = 0;
   m_relatedObject = relatedObject;
   m_relatedDCI = relatedDCI;
   m_currentTicket = 0;
   m_serviceId = serviceId;
   m_statusThreshold = threshhold;
   m_mutex = MutexCreateFast();
}

/**
 * Create copy of existing business service check
 */
BusinessServiceCheck::BusinessServiceCheck(uint32_t serviceId, const BusinessServiceCheck& check) : m_description(check.getDescription())
{
   m_id = CreateUniqueId(IDG_BUSINESS_SERVICE_CHECK);
	m_type = check.m_type;
   m_status = STATUS_NORMAL;
	m_script = MemCopyString(check.m_script);
	m_compiledScript = nullptr;
	m_reason[0] = 0;
	m_relatedObject = check.m_relatedObject;
	m_relatedDCI = check.m_relatedDCI;
	m_currentTicket = 0;
	m_serviceId = serviceId;
	m_statusThreshold = check.m_statusThreshold;
	m_mutex = MutexCreateFast();
}

/**
 * Create business service check from database
 */
BusinessServiceCheck::BusinessServiceCheck(DB_RESULT hResult, int row)
{
   m_id = DBGetFieldULong(hResult, row, 0);
   m_serviceId = DBGetFieldULong(hResult, row, 1);
   m_type = BusinessServiceCheckTypeFromInt(DBGetFieldLong(hResult, row, 2));
   m_description = DBGetFieldAsSharedString(hResult, row, 3);
   m_relatedObject = DBGetFieldULong(hResult, row, 4);
   m_relatedDCI = DBGetFieldULong(hResult, row, 5);
   m_statusThreshold = DBGetFieldULong(hResult, row, 6);
   m_script = DBGetField(hResult, row, 7, nullptr, 0);
   m_currentTicket = DBGetFieldULong(hResult, row, 8);
   m_mutex = MutexCreateFast();
   m_compiledScript = nullptr;
   compileScript();
   m_reason[0] = 0;
   loadReason();
}

/**
 * Load reason of violated business service check
 */
void BusinessServiceCheck::loadReason()
{
   if (m_currentTicket != 0)
   {
      DB_HANDLE hdb = DBConnectionPoolAcquireConnection();
      DB_STATEMENT hStmt = DBPrepare(hdb, _T("SELECT reason FROM business_service_tickets WHERE ticket_id=?"));
      if (hStmt != nullptr)
      {
         DBBind(hStmt, 1, DB_SQLTYPE_INTEGER, m_currentTicket);
         DB_RESULT hResult = DBSelectPrepared(hStmt);
         if (hResult != nullptr)
         {
            DBGetField(hResult, 0, 0, m_reason, 256);
            DBFreeResult(hResult);
         }
         DBFreeStatement(hStmt);
      }
      DBConnectionPoolReleaseConnection(hdb);
   }
}

/**
 * Service class destructor
 */
BusinessServiceCheck::~BusinessServiceCheck()
{
	MemFree(m_script);
	delete m_compiledScript;
	MutexDestroy(m_mutex);
}

/**
 * Modify check from request
 */
void BusinessServiceCheck::modifyFromMessage(const NXCPMessage& request)
{
	lock();
	if (request.isFieldExist(VID_BIZSVC_CHECK_TYPE))
   {
      m_type = BusinessServiceCheckTypeFromInt(request.getFieldAsInt16(VID_BIZSVC_CHECK_TYPE));
   }
	if (request.isFieldExist(VID_RELATED_OBJECT))
   {
      m_relatedObject = request.getFieldAsUInt32(VID_RELATED_OBJECT);
   }
	if (request.isFieldExist(VID_RELATED_DCI))
   {
      m_relatedDCI = request.getFieldAsUInt32(VID_RELATED_DCI);
   }
	if (request.isFieldExist(VID_SCRIPT))
   {
		MemFree(m_script);
      m_script = request.getFieldAsString(VID_SCRIPT);
		compileScript();
   }
	if (request.isFieldExist(VID_DESCRIPTION))
   {
      m_description = request.getFieldAsSharedString(VID_DESCRIPTION);
   }
	if (request.isFieldExist(VID_THRESHOLD))
   {
      m_statusThreshold = request.getFieldAsInt32(VID_THRESHOLD);
   }
	unlock();
}

/**
 * Compile script if there is one
 */
void BusinessServiceCheck::compileScript()
{
	if ((m_type != BusinessServiceCheckType::SCRIPT) || (m_script == nullptr))
	   return;

	delete m_compiledScript;
   TCHAR errorMsg[256];
	m_compiledScript = NXSLCompile(m_script, errorMsg, sizeof(errorMsg) / sizeof(TCHAR), nullptr);
   if (m_compiledScript == nullptr)
   {
      TCHAR buffer[1024];
      _sntprintf(buffer, 1024, _T("BusinessServiceCheck::%u"), m_id);
      PostSystemEvent(EVENT_SCRIPT_ERROR, g_dwMgmtNode, "ssd", buffer, errorMsg, 0);
      nxlog_write(NXLOG_WARNING, _T("Failed to compile script for service check %s [%u] (%s)"), m_description.cstr(), m_id, errorMsg);
   }
}

/**
 * Fill message with business service check data
 */
void BusinessServiceCheck::fillMessage(NXCPMessage *msg, uint32_t baseId) const
{
	lock();
   msg->setField(baseId, m_id);
   msg->setField(baseId + 1, static_cast<uint16_t>(m_type));
   msg->setField(baseId + 2, m_reason);
   msg->setField(baseId + 3, m_relatedDCI);
   msg->setField(baseId + 4, m_relatedObject);
   msg->setField(baseId + 5, m_statusThreshold);
   msg->setField(baseId + 6, m_description);
   msg->setField(baseId + 7, m_script);
	unlock();
}

/**
 * Save business service check to database
 */
bool BusinessServiceCheck::saveToDatabase() const
{
   DB_HANDLE hdb = DBConnectionPoolAcquireConnection();

   static const TCHAR *columns[] = {
         _T("service_id") ,_T("type"), _T("description"), _T("related_object"), _T("related_dci"), _T("status_threshold"),
         _T("content"), _T("current_ticket"), nullptr
   };
   DB_STATEMENT hStmt = DBPrepareMerge(hdb, _T("business_service_checks"), _T("id"), m_id, columns);
	bool success = false;
	if (hStmt != nullptr)
	{
		lock();
		DBBind(hStmt, 1, DB_SQLTYPE_INTEGER, m_serviceId);
		DBBind(hStmt, 2, DB_SQLTYPE_INTEGER, static_cast<uint32_t>(m_type));
		DBBind(hStmt, 3, DB_SQLTYPE_VARCHAR, m_description, DB_BIND_STATIC);
		DBBind(hStmt, 4, DB_SQLTYPE_INTEGER, m_relatedObject);
		DBBind(hStmt, 5, DB_SQLTYPE_INTEGER, m_relatedDCI);
		DBBind(hStmt, 6, DB_SQLTYPE_INTEGER, m_statusThreshold);
		DBBind(hStmt, 7, DB_SQLTYPE_TEXT, m_script, DB_BIND_STATIC);
		DBBind(hStmt, 8, DB_SQLTYPE_INTEGER, m_currentTicket);
		DBBind(hStmt, 9, DB_SQLTYPE_INTEGER, m_id);
		success = DBExecute(hStmt);
      unlock();
		DBFreeStatement(hStmt);
	}
	else
	{
		success = false;
	}
	DBConnectionPoolReleaseConnection(hdb);

	return success;
}

/**
 * Delete business service check from database
 */
bool BusinessServiceCheck::deleteFromDatabase()
{
	DB_HANDLE hdb = DBConnectionPoolAcquireConnection();
	bool success = ExecuteQueryOnObject(hdb, m_id, _T("DELETE FROM business_service_checks WHERE id=?"));
	DBConnectionPoolReleaseConnection(hdb);
	return success;
}

/**
 * Execute check. It could be object status check, or DCI status check or script
 */
int BusinessServiceCheck::execute(BusinessServiceTicketData* ticket)
{
	lock();
	int oldStatus = m_status;
	switch (m_type)
	{
		case BusinessServiceCheckType::OBJECT:
			{
				shared_ptr<NetObj> obj = FindObjectById(m_relatedObject);
				if (obj != nullptr)
				{
					int threshold = m_statusThreshold != 0 ? m_statusThreshold : ConfigReadInt(_T("BusinessServices.Check.Threshold.Objects"), STATUS_WARNING);
					m_status = obj->getStatus() >= threshold ? STATUS_CRITICAL : STATUS_NORMAL;
					_tcslcpy(m_reason, _T("Object status threshold violation"), 256);
				}
			}
			break;
		case BusinessServiceCheckType::SCRIPT:
			if (m_compiledScript != nullptr)
			{
				NXSL_VM *vm = CreateServerScriptVM(m_compiledScript, FindObjectById(m_relatedObject));
				if (vm != nullptr)
				{
					vm->addConstant("OK", vm->createValue(true));
					vm->addConstant("FAIL", vm->createValue(false));
					vm->setGlobalVariable("$reason", vm->createValue());
					shared_ptr<NetObj> serviceObject = FindObjectById(m_serviceId);
					if (serviceObject != nullptr)
					   vm->setGlobalVariable("$service", serviceObject->createNXSLObject(vm));
					NXSL_VariableSystem *globals = nullptr;
					ObjectRefArray<NXSL_Value> args(0);
					if (vm->run(args, &globals))
					{
						NXSL_Value *value = vm->getResult();
						if (value->getDataType() == NXSL_DT_STRING)
						{
							m_status = STATUS_CRITICAL;
							_tcslcpy(m_reason, value->getValueAsCString(), 256);
						}
						else
						{
							if (value->isBoolean())
							{
								m_status = value->isTrue() ? STATUS_NORMAL : STATUS_CRITICAL;
							}
							else
							{
								m_status = STATUS_NORMAL;
							}
							if (m_status == STATUS_CRITICAL)
							{
								NXSL_Variable *reason = globals->find("$reason");
								if ((reason != nullptr) && (reason->getValue()->getValueAsCString()[0] != 0))
								{
									_tcslcpy(m_reason, reason->getValue()->getValueAsCString(), 256);
								}
								else
								{
									_tcscpy(m_reason, _T("Check script returned error"));
								}
							}
						}
						delete globals;
					}
					else
					{
						TCHAR buffer[1024];
						_sntprintf(buffer, 1024, _T("BusinessServiceCheck::%u"), m_id);
						PostSystemEvent(EVENT_SCRIPT_ERROR, g_dwMgmtNode, "ssd", buffer, vm->getErrorText(), 0);
						nxlog_write_tag(2, DEBUG_TAG, _T("Failed to execute script for service check object %s [%u] (%s)"), m_description.cstr(), m_id, vm->getErrorText());
						m_status = STATUS_NORMAL;
					}
					delete vm;
				}
				else
				{
					m_status = STATUS_NORMAL;
				}
			}
			else
			{
				m_status = STATUS_NORMAL;
			}
			break;
		case BusinessServiceCheckType::DCI:
			{
				shared_ptr<NetObj> object = FindObjectById(m_relatedObject);
				if ((object != nullptr) && object->isDataCollectionTarget())
				{
					int threshold = m_statusThreshold != 0 ? m_statusThreshold : ConfigReadInt(_T("BusinessServices.Check.Threshold.DataCollection"), STATUS_WARNING);
					m_status = static_cast<DataCollectionTarget&>(*object).getDciThreshold(m_relatedDCI) >= threshold ? STATUS_CRITICAL : STATUS_NORMAL;
					_tcslcpy(m_reason, _T("DCI threshold violation"), 256);
				}
			}
			break;
		default:
			nxlog_write_tag(4, DEBUG_TAG, _T("BusinessServiceCheck::execute(%s [%u]) called for undefined check type %d"), m_description.cstr(), m_id, m_type);
			m_status = STATUS_NORMAL;
			break;
	}

	if (m_status != oldStatus)
	{
		if (m_status == STATUS_CRITICAL)
		{
			insertTicket(ticket);
		}
		else
		{
			closeTicket();
			m_reason[0] = 0;
		}
	}
	int newStatus = m_status;
	unlock();
	return newStatus;
}

/**
 * Insert ticket for this check into business_service_tickets. Expected to be called while lock on check is held.
 */
bool BusinessServiceCheck::insertTicket(BusinessServiceTicketData* ticket)
{
	if (m_status == STATUS_NORMAL)
		return false;

	m_currentTicket = CreateUniqueId(IDG_BUSINESS_SERVICE_TICKET);

	bool success = false;
	time_t currentTime = time(nullptr);
	DB_HANDLE hdb = DBConnectionPoolAcquireConnection();
	DB_STATEMENT hStmt = DBPrepare(hdb, _T("INSERT INTO business_service_tickets (ticket_id,original_ticket_id,original_service_id,check_id,check_description,service_id,create_timestamp,close_timestamp,reason) VALUES (?,0,0,?,?,?,?,0,?)"));
	if (hStmt != nullptr)
	{
		DBBind(hStmt, 1, DB_SQLTYPE_INTEGER, m_currentTicket);
		DBBind(hStmt, 2, DB_SQLTYPE_INTEGER, m_id);
		DBBind(hStmt, 3, DB_SQLTYPE_VARCHAR, m_description, DB_BIND_STATIC);
		DBBind(hStmt, 4, DB_SQLTYPE_INTEGER, m_serviceId);
		DBBind(hStmt, 5, DB_SQLTYPE_INTEGER, static_cast<uint32_t>(currentTime));
		DBBind(hStmt, 6, DB_SQLTYPE_VARCHAR, m_reason, DB_BIND_STATIC);
		success = DBExecute(hStmt);
		DBFreeStatement(hStmt);
	}

	if (success)
	{
		ticket->ticketId = m_currentTicket;
		ticket->checkId = m_id;
		_tcslcpy(ticket->description, m_description, 1024);
		ticket->serviceId = m_serviceId;
		ticket->timestamp = currentTime;
		_tcslcpy(ticket->reason, m_reason, 256);

		hStmt = DBPrepare(hdb, _T("UPDATE business_service_checks SET current_ticket=? WHERE id=?"));
		if (hStmt != nullptr)
		{
			DBBind(hStmt, 1, DB_SQLTYPE_INTEGER, m_currentTicket);
			DBBind(hStmt, 2, DB_SQLTYPE_INTEGER, m_id);
			success = DBExecute(hStmt);
			DBFreeStatement(hStmt);
		}
	}

	DBConnectionPoolReleaseConnection(hdb);
	return success;
}

/**
 * Close current ticket.  Expected to be called while lock on check is held.
 */
void BusinessServiceCheck::closeTicket()
{
	DB_HANDLE hdb = DBConnectionPoolAcquireConnection();

	DB_STATEMENT hStmt = DBPrepare(hdb, _T("UPDATE business_service_tickets SET close_timestamp=? WHERE ticket_id=? OR original_ticket_id=?"));
	if (hStmt != nullptr)
	{
		DBBind(hStmt, 1, DB_SQLTYPE_INTEGER, static_cast<uint32_t>(time(nullptr)));
		DBBind(hStmt, 2, DB_SQLTYPE_INTEGER, m_currentTicket);
		DBBind(hStmt, 3, DB_SQLTYPE_INTEGER, m_currentTicket);
		DBExecute(hStmt);
		DBFreeStatement(hStmt);
	}

	DBConnectionPoolReleaseConnection(hdb);

	m_currentTicket = 0;
	m_reason[0] = 0;
}