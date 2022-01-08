/* 
** NetXMS - Network Management System
** NetXMS Scripting Language Interpreter
** Copyright (C) 2003-2021 Victor Kirhenshtein
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU Lesser General Public License as published by
** the Free Software Foundation; either version 3 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU Lesser General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
** File: vm.cpp
**/

#include "libnxsl.h"
#include <netxms-regex.h>

/**
 * Constants
 */
#define MAX_ERROR_NUMBER         40
#define CONTROL_STACK_LIMIT      32768

/**
 * Class registry
 */
extern NXSL_ClassRegistry g_nxslClassRegistry;

/**
 * Error texts
 */
static const TCHAR *s_runtimeErrorMessage[MAX_ERROR_NUMBER] =
{
	_T("Data stack underflow"),
	_T("Control stack underflow"),
	_T("Condition value is not a number"),
	_T("Bad arithmetic conversion"),
	_T("Invalid operation with NULL value"),
	_T("Internal error"),
	_T("main() function not presented"),
	_T("Control stack overflow"),
	_T("Divide by zero"),
	_T("Invalid operation with real numbers"),
	_T("Function not found"),
	_T("Invalid number of function's arguments"),
	_T("Cannot do automatic type cast"),
	_T("Function or operation argument is not an object"),
	_T("Unknown object's attribute"),
	_T("Requested module not found or cannot be loaded"),
	_T("Argument is not of string type and cannot be converted to string"),
	_T("Invalid regular expression"),
	_T("Function or operation argument is not a whole number"),
	_T("Invalid operation on object"),
	_T("Bad (or incompatible) object class"),
	_T("Variable already exist"),
	_T("Array index is not an integer"),
	_T("Attempt to use array element access operation on non-array"),
	_T("Cannot assign to a variable that is constant"),
	_T("Named parameter required"),
	_T("Function or operation argument is not an iterator"),
	_T("Statistical data for given instance is not collected yet"),
	_T("Requested statistical parameter does not exist"),
	_T("Unknown object's method"),
   _T("Constant not defined"),
   _T("Execution aborted"),
	_T("Attempt to use hash map element access operation on non hash map"),
   _T("Function or operation argument is not a container"),
   _T("Hash map key is not a string"),
   _T("Selector not found"),
   _T("Object constructor not found"),
   _T("Invalid number of object constructor's arguments"),
   _T("Assertion failed"),
   _T("Function or operation argument cannot be interpreted as boolean value")
};

/**
 * Position number to variable name in form $<position>
 */
static inline void PositionToVarName(int n, char *varName)
{
   varName[0] = '$';
   if (n < 10)
   {
      varName[1] = n + '0';
      varName[2] = 0;
   }
   else if (n < 100)
   {
      varName[1] = n / 10 + '0';
      varName[2] = n % 10 + '0';
      varName[3] = 0;
   }
   else
   {
      varName[1] = n / 100 + '0';
      varName[2] = (n % 100) / 10 + '0';
      varName[3] = n % 10 + '0';
      varName[4] = 0;
   }
}

/**
 * Get error message for given error code
 */
static const TCHAR *GetErrorMessage(int error)
{
   return ((error > 0) && (error <= MAX_ERROR_NUMBER)) ? s_runtimeErrorMessage[error - 1] : _T("Unknown error code");
}

/**
 * Determine operation data type
 */
static int SelectResultType(int nType1, int nType2, int nOp)
{
   int nType;

   if (nOp == OPCODE_DIV)
   {
      nType = NXSL_DT_REAL;
   }
   else
   {
      if ((nType1 == NXSL_DT_REAL) || (nType2 == NXSL_DT_REAL))
      {
         if ((nOp == OPCODE_REM) || (nOp == OPCODE_LSHIFT) ||
             (nOp == OPCODE_RSHIFT) || (nOp == OPCODE_BIT_AND) ||
             (nOp == OPCODE_BIT_OR) || (nOp == OPCODE_BIT_XOR))
         {
            nType = NXSL_DT_NULL;   // Error
         }
         else
         {
            nType = NXSL_DT_REAL;
         }
      }
      else
      {
         if (((nType1 >= NXSL_DT_UINT32) && (nType2 < NXSL_DT_UINT32)) ||
             ((nType1 < NXSL_DT_UINT32) && (nType2 >= NXSL_DT_UINT32)))
         {
            // One operand signed, other unsigned, convert both to signed
            if (nType1 >= NXSL_DT_UINT32)
               nType1 -= 2;
            else if (nType2 >= NXSL_DT_UINT32)
               nType2 -= 2;
         }
         nType = std::max(nType1, nType2);
      }
   }
   return nType;
}

/**
 * Security context destructor
 */
NXSL_SecurityContext::~NXSL_SecurityContext()
{
}

/**
 * Validate access with security context
 */
bool NXSL_SecurityContext::validateAccess(int accessType, const void *object)
{
   return true;
}

/**
 * Constructor
 */
NXSL_VM::NXSL_VM(NXSL_Environment *env, NXSL_Storage *storage) : NXSL_ValueManager(), m_instructionSet(256, 256),
         m_functions(0, 16), m_modules(0, 16, Ownership::True)
{
   m_cp = INVALID_ADDRESS;
   m_stopFlag = false;
   m_errorCode = 0;
   m_errorLine = 0;
   m_errorText = nullptr;
   m_assertMessage = nullptr;
   m_constants = nullptr;
   m_globalVariables = new NXSL_VariableSystem(this, NXSL_VariableSystemType::GLOBAL);
   m_localVariables = nullptr;
   m_expressionVariables = nullptr;
   m_exportedExpressionVariables = nullptr;
   m_contextVariables = nullptr;
   m_context = nullptr;
   m_securityContext = nullptr;
   m_subLevel = 0;    // Level of current subroutine
   m_env = (env != nullptr) ? env : new NXSL_Environment;
   m_pRetValue = nullptr;
	m_userData = nullptr;
	m_nBindPos = 0;
	if (storage != nullptr)
	{
      m_localStorage = nullptr;
	   m_storage = storage;
	}
	else
	{
      m_localStorage = new NXSL_LocalStorage(this);
      m_storage = m_localStorage;
	}
}

/**
 * Destructor
 */
NXSL_VM::~NXSL_VM()
{
   for(int i = 0; i < m_instructionSet.size(); i++)
      m_instructionSet.get(i)->dispose(this);

   delete m_constants;
   delete m_globalVariables;
   delete m_localVariables;
   delete m_expressionVariables;
   delete m_contextVariables;
   destroyValue(m_context);
   delete m_securityContext;

   delete m_localStorage;

   delete m_env;
   destroyValue(m_pRetValue);

   MemFree(m_errorText);
   MemFree(m_assertMessage);
}

/**
 * Constant creation callback
 */
EnumerationCallbackResult NXSL_VM::createConstantsCallback(const void *key, void *value, void *data)
{
   static_cast<NXSL_VM*>(data)->m_constants->create(*static_cast<const NXSL_Identifier*>(key),
            static_cast<NXSL_VM*>(data)->createValue(static_cast<NXSL_Value*>(value)));
   return _CONTINUE;
}

/**
 * Load program
 */
bool NXSL_VM::load(const NXSL_Program *program)
{
   bool success = true;

   // Copy metadata
   m_metadata.clear();
   m_metadata.addAll(program->m_metadata);

   // Copy instructions
   for(int i = 0; i < m_instructionSet.size(); i++)
      m_instructionSet.get(i)->dispose(this);
   m_instructionSet.clear();
   for(int i = 0; i < program->m_instructionSet.size(); i++)
      m_instructionSet.addPlaceholder()->copyFrom(program->m_instructionSet.get(i), this);

   // Copy function information
   m_functions.clear();
   for(int i = 0; i < program->m_functions.size(); i++)
      m_functions.add(NXSL_Function(program->m_functions.get(i)));

   // Set constants
   if (program->m_constants.size() > 0)
   {
      if (m_constants != nullptr)
         m_constants->clear();
      else
         m_constants = new NXSL_VariableSystem(this, NXSL_VariableSystemType::CONSTANT);
      program->m_constants.forEach(createConstantsCallback, this);
   }
   else
   {
      delete_and_null(m_constants);
   }

   // Load modules
   m_modules.clear();
   for(int i = 0; i < program->m_requiredModules.size(); i++)
   {
      const NXSL_ModuleImport *importInfo = program->m_requiredModules.get(i);
      if (!m_env->loadModule(this, importInfo))
      {
         error(NXSL_ERR_MODULE_NOT_FOUND, importInfo->lineNumber);
         success = false;
         break;
      }
   }

   return success;
}

/**
 * Run program
 * Returns true on success and false on error
 */
bool NXSL_VM::run(int argc, NXSL_Value **argv, NXSL_VariableSystem **globals,
         NXSL_VariableSystem **expressionVariables, NXSL_VariableSystem *constants, const char *entryPoint)
{
   ObjectRefArray<NXSL_Value> args(argc, 8);
   for(int i = 0; i < argc; i++)
      args.add(argv[i]);
   return run(args, globals, expressionVariables, constants, entryPoint);
}

/**
 * Run program
 * Returns true on success and false on error
 */
bool NXSL_VM::run(const ObjectRefArray<NXSL_Value>& args, NXSL_VariableSystem **globals,
         NXSL_VariableSystem **expressionVariables, NXSL_VariableSystem *constants, const char *entryPoint)
{
	m_cp = INVALID_ADDRESS;

   // Delete previous return value
	destroyValue(m_pRetValue);
	m_pRetValue = nullptr;

   m_dataStack.reset();
   m_codeStack.reset();
   m_catchStack.reset();

   // Preserve original global variables and constants
   NXSL_VariableSystem *savedGlobals = new NXSL_VariableSystem(this, m_globalVariables);
   NXSL_VariableSystem *savedConstants = (m_constants != nullptr) ? new NXSL_VariableSystem(this, m_constants) : nullptr;
   if (constants != nullptr)
   {
      if (m_constants == nullptr)
         m_constants = new NXSL_VariableSystem(this, NXSL_VariableSystemType::CONSTANT);
      m_constants->merge(constants);
   }

   // Create local variable system for main() and bind arguments
   NXSL_Array *argsArray = new NXSL_Array(this);
   m_localVariables = new NXSL_VariableSystem(this, NXSL_VariableSystemType::LOCAL);
   for(int i = 0; i < args.size(); i++)
   {
      argsArray->set(i + 1, createValue(args.get(i)));
      char name[32];
      PositionToVarName(i + 1, name);
      m_localVariables->create(name, args.get(i));
   }
   setGlobalVariable("$ARGS", createValue(argsArray));

   // If not nullptr last used expression variables will be saved there
   m_exportedExpressionVariables = expressionVariables;

	m_env->configureVM(this);

   // Locate entry point and run
   uint32_t entryAddr = INVALID_ADDRESS;
	if (entryPoint != nullptr)
	{
      entryAddr = getFunctionAddress(entryPoint);
	}
	else
	{
      entryAddr = getFunctionAddress("main");

		// No explicit main(), search for implicit
		if (entryAddr == INVALID_ADDRESS)
		{
         entryAddr = getFunctionAddress("$main");
		}
	}

   if (entryAddr != INVALID_ADDRESS)
   {
      m_cp = entryAddr;
      m_stopFlag = false;
resume:
      while((m_cp < static_cast<uint32_t>(m_instructionSet.size())) && !m_stopFlag)
         execute();
      if (!m_stopFlag)
      {
         if (m_cp != INVALID_ADDRESS)
         {
            m_pRetValue = m_dataStack.pop();
            if (m_pRetValue == nullptr)
            {
               error(NXSL_ERR_DATA_STACK_UNDERFLOW);
            }
         }
         else if (m_catchStack.getPosition() > 0)
         {
            if (unwind())
            {
               setGlobalVariable("$errorcode", createValue(m_errorCode));
               setGlobalVariable("$errorline", createValue(m_errorLine));
               setGlobalVariable("$errormsg", createValue(GetErrorMessage(m_errorCode)));
               setGlobalVariable("$errortext", createValue(m_errorText));
               goto resume;
            }
         }
      }
      else
      {
         error(NXSL_ERR_EXECUTION_ABORTED);
      }
   }
   else
   {
      error(NXSL_ERR_NO_MAIN);
   }

   // Restore instructions replaced to direct variable pointers
   m_localVariables->restoreVariableReferences(&m_instructionSet);
   m_globalVariables->restoreVariableReferences(&m_instructionSet);
   if (m_constants != nullptr)
      m_constants->restoreVariableReferences(&m_instructionSet);
   if (m_expressionVariables != nullptr)
      m_expressionVariables->restoreVariableReferences(&m_instructionSet);

   // Restore global variables
   if (globals == nullptr)
	   delete m_globalVariables;
	else
		*globals = m_globalVariables;
   m_globalVariables = savedGlobals;

	// Restore constants
   delete m_constants;
   m_constants = savedConstants;

   // Cleanup
	NXSL_Value *v;
   while((v = m_dataStack.pop()) != nullptr)
      destroyValue(v);

   while(m_subLevel > 0)
   {
      m_subLevel--;

      // Expression variables
      auto variableSystem = static_cast<NXSL_VariableSystem*>(m_codeStack.pop());
      if (variableSystem != nullptr)
      {
         variableSystem->restoreVariableReferences(&m_instructionSet);
         delete variableSystem;
      }

      // Local variables
      variableSystem = static_cast<NXSL_VariableSystem*>(m_codeStack.pop());
      if (variableSystem != nullptr)
      {
         variableSystem->restoreVariableReferences(&m_instructionSet);
         delete variableSystem;
      }

      m_codeStack.pop();
   }

   NXSL_CatchPoint *p;
   while((p = m_catchStack.pop()) != nullptr)
      delete p;

   delete_and_null(m_localVariables);
   delete_and_null(m_expressionVariables);

   return (m_cp != INVALID_ADDRESS);
}

/**
 * Unwind stack to nearest catch
 */
bool NXSL_VM::unwind()
{
   NXSL_CatchPoint *p = m_catchStack.pop();
   if (p == nullptr)
      return false;

   while(m_subLevel > p->subLevel)
   {
      m_subLevel--;

      if (m_expressionVariables != nullptr)
      {
         m_expressionVariables->restoreVariableReferences(&m_instructionSet);
         delete m_expressionVariables;
      }
      m_expressionVariables = static_cast<NXSL_VariableSystem*>(m_codeStack.pop());

      m_localVariables->restoreVariableReferences(&m_instructionSet);
      delete m_localVariables;
      m_localVariables = static_cast<NXSL_VariableSystem*>(m_codeStack.pop());

      m_codeStack.pop();
   }

   while(m_dataStack.getPosition() > p->dataStackSize)
      destroyValue(m_dataStack.pop());

   m_cp = p->addr;
   delete p;
   return true;
}

/**
 * Add constant to VM
 */
bool NXSL_VM::addConstant(const NXSL_Identifier& name, NXSL_Value *value)
{
   if (isDefinedConstant(name))
      return false;  // Already defined

   if (m_constants == nullptr)
      m_constants = new NXSL_VariableSystem(this, NXSL_VariableSystemType::CONSTANT);
   m_constants->create(name, value);
   return true;
}

/**
 * Set global variale
 */
void NXSL_VM::setGlobalVariable(const NXSL_Identifier& name, NXSL_Value *pValue)
{
   NXSL_Variable *var = m_globalVariables->find(name);
   if (var == nullptr)
		m_globalVariables->create(name, pValue);
	else
		var->setValue(pValue);
}

/**
 * Find variable
 */
NXSL_Variable *NXSL_VM::findVariable(const NXSL_Identifier& name, NXSL_VariableSystem **vs)
{
   NXSL_Variable *var = (m_constants != nullptr) ? m_constants->find(name) : nullptr;
   if (var != nullptr)
   {
      if (vs != nullptr)
         *vs = m_constants;
      return var;
   }

   var = m_globalVariables->find(name);
   if (var != nullptr)
   {
      if (vs != nullptr)
         *vs = m_globalVariables;
      return var;
   }

   if (m_context != nullptr)
   {
      NXSL_Object *object = m_context->getValueAsObject();
      NXSL_Value *value = object->getClass()->getAttr(object, name.value);
      if (value != nullptr)
      {
         var = m_contextVariables->find(name);
         if (var != nullptr)
            var->setValue(value);
         else
            var = m_contextVariables->create(name, value);
         if (vs != nullptr)
            *vs = m_contextVariables;
         return var;
      }
   }

   var = m_localVariables->find(name);
   if (var != nullptr)
   {
      if (vs != nullptr)
         *vs = m_localVariables;
      return var;
   }

   if (m_expressionVariables != nullptr)
   {
      var = m_expressionVariables->find(name);
      if (var != nullptr)
      {
         if (vs != nullptr)
            *vs = m_expressionVariables;
         return var;
      }
   }

   return nullptr;
}

/**
 * Find variable or create if does not exist
 */
NXSL_Variable *NXSL_VM::findOrCreateVariable(const NXSL_Identifier& name, NXSL_VariableSystem **vs)
{
   NXSL_Variable *var = findVariable(name, vs);
   if (var == nullptr)
   {
      var = m_localVariables->create(name);
      if (vs != nullptr)
         *vs = m_localVariables;
   }
   return var;
}

/**
 * Create variable if it does not exist, otherwise return nullptr
 */
NXSL_Variable *NXSL_VM::createVariable(const NXSL_Identifier& name)
{
   NXSL_Variable *var = nullptr;
   if (!isDefinedConstant(name) &&
       (m_globalVariables->find(name) == nullptr) &&
       (m_localVariables->find(name) == nullptr))
   {
      var = m_localVariables->create(name);
   }
   return var;
}

/**
 * Check if given name points to defined constant (either by environment or in constant list)
 */
bool NXSL_VM::isDefinedConstant(const NXSL_Identifier& name)
{
   if ((m_constants != nullptr) && (m_constants->find(name) != nullptr))
      return true;
   NXSL_Value *v = m_env->getConstantValue(name, this);
   if (v != nullptr)
   {
      destroyValue(v);
      return true;
   }
   return false;
}

/**
 * Execute single instruction
 */
void NXSL_VM::execute()
{
   NXSL_Instruction *cp;
   NXSL_Value *pValue;
   NXSL_Variable *pVar;
   const NXSL_ExtFunction *pFunc;
   uint32_t dwNext = m_cp + 1;
   char varName[MAX_IDENTIFIER_LENGTH];
   int i, nRet;
   bool constructor;
   NXSL_VariableSystem *vs;

   cp = m_instructionSet.get(m_cp);
   switch(cp->m_opCode)
   {
      case OPCODE_PUSH_CONSTANT:
         m_dataStack.push(createValue(cp->m_operand.m_constant));
         break;
      case OPCODE_PUSH_NULL:
         m_dataStack.push(createValue());
         break;
      case OPCODE_PUSH_TRUE:
         m_dataStack.push(createValue(true));
         break;
      case OPCODE_PUSH_FALSE:
         m_dataStack.push(createValue(false));
         break;
      case OPCODE_PUSH_INT32:
         m_dataStack.push(createValue(cp->m_operand.m_valueInt32));
         break;
      case OPCODE_PUSH_UINT32:
         m_dataStack.push(createValue(cp->m_operand.m_valueUInt32));
         break;
      case OPCODE_PUSH_INT64:
         m_dataStack.push(createValue(cp->m_operand.m_valueInt64));
         break;
      case OPCODE_PUSH_UINT64:
         m_dataStack.push(createValue(cp->m_operand.m_valueUInt64));
         break;
      case OPCODE_PUSH_VARIABLE:
         pValue = m_env->getConstantValue(*cp->m_operand.m_identifier, this);
         if (pValue != nullptr)
         {
            m_dataStack.push(pValue);
         }
         else
         {
            pVar = findOrCreateVariable(*cp->m_operand.m_identifier, &vs);
            m_dataStack.push(createValue(pVar->getValue()));
            // convert to direct variable access without name lookup
            if (vs->createVariableReferenceRestorePoint(m_cp, cp->m_operand.m_identifier))
            {
               cp->m_opCode = OPCODE_PUSH_VARPTR;
               cp->m_operand.m_variable = pVar;
            }
         }
         break;
      case OPCODE_PUSH_VARPTR:
         m_dataStack.push(createValue(cp->m_operand.m_variable->getValue()));
         break;
      case OPCODE_PUSH_EXPRVAR:
         if (m_expressionVariables == nullptr)
            m_expressionVariables = new NXSL_VariableSystem(this, NXSL_VariableSystemType::EXPRESSION);

         pVar = m_expressionVariables->find(*cp->m_operand.m_identifier);
         if (pVar != nullptr)
         {
            m_dataStack.push(createValue(pVar->getValue()));
            // convert to direct variable access without name lookup
            if (m_expressionVariables->createVariableReferenceRestorePoint(m_cp, cp->m_operand.m_identifier))
            {
               cp->m_opCode = OPCODE_PUSH_VARPTR;
               cp->m_operand.m_variable = pVar;
            }
            dwNext++;   // Skip next instruction
         }
         else if (m_subLevel < CONTROL_STACK_LIMIT)
         {
            m_subLevel++;
            m_codeStack.push(CAST_TO_POINTER(m_cp + 1, void *));
            m_codeStack.push(nullptr);
            m_codeStack.push(m_expressionVariables);
            if (m_expressionVariables != nullptr)
            {
               m_expressionVariables->restoreVariableReferences(&m_instructionSet);
               m_expressionVariables = nullptr;
            }
            dwNext = cp->m_addr2;
         }
         else
         {
            error(NXSL_ERR_CONTROL_STACK_OVERFLOW);
         }
         break;
      case OPCODE_UPDATE_EXPRVAR:
         if (m_exportedExpressionVariables == nullptr)
         {
            dwNext++;   // Skip next instruction
            break;   // no need for update
         }

         if (m_expressionVariables == nullptr)
            m_expressionVariables = new NXSL_VariableSystem(this, NXSL_VariableSystemType::EXPRESSION);

         pVar = m_expressionVariables->find(*cp->m_operand.m_identifier);
         if (pVar != nullptr)
         {
            dwNext++;   // Skip next instruction
         }
         else if (m_subLevel < CONTROL_STACK_LIMIT)
         {
            m_subLevel++;
            m_codeStack.push(CAST_TO_POINTER(m_cp + 1, void *));
            m_codeStack.push(nullptr);
            m_codeStack.push(m_expressionVariables);
            if (m_expressionVariables != nullptr)
            {
               m_expressionVariables->restoreVariableReferences(&m_instructionSet);
               m_expressionVariables = nullptr;
            }
            dwNext = cp->m_addr2;
         }
         else
         {
            error(NXSL_ERR_CONTROL_STACK_OVERFLOW);
         }
         break;
      case OPCODE_PUSH_CONSTREF:
         pValue = m_env->getConstantValue(*cp->m_operand.m_identifier, this);
         if (pValue != nullptr)
         {
            m_dataStack.push(pValue);
         }
         else if (m_constants != nullptr)
         {
            pVar = m_constants->find(*cp->m_operand.m_identifier);
            if (pVar != nullptr)
            {
               m_dataStack.push(createValue(pVar->getValue()));
               // convert to direct value access without name lookup
               if (m_constants->createVariableReferenceRestorePoint(m_cp, cp->m_operand.m_identifier))
               {
                  cp->m_opCode = OPCODE_PUSH_VARPTR;
                  cp->m_operand.m_variable = pVar;
               }
            }
            else
            {
               error(NXSL_ERR_NO_SUCH_CONSTANT);
            }
         }
         else
         {
            error(NXSL_ERR_NO_SUCH_CONSTANT);
         }
         break;
      case OPCODE_CLEAR_EXPRVARS:
         if (m_expressionVariables != nullptr)
            m_expressionVariables->restoreVariableReferences(&m_instructionSet);
         if (m_exportedExpressionVariables != nullptr)
         {
            delete *m_exportedExpressionVariables;
            *m_exportedExpressionVariables = m_expressionVariables;
            m_expressionVariables = nullptr;
         }
         else
         {
            delete_and_null(m_expressionVariables);
         }
         break;
      case OPCODE_PUSH_PROPERTY:
         pushProperty(*cp->m_operand.m_identifier);
         break;
      case OPCODE_NEW_ARRAY:
         m_dataStack.push(createValue(new NXSL_Array(this)));
         break;
      case OPCODE_NEW_HASHMAP:
         m_dataStack.push(createValue(new NXSL_HashMap(this)));
         break;
      case OPCODE_SET:
         pVar = findOrCreateVariable(*cp->m_operand.m_identifier, &vs);
			if (!pVar->isConstant())
			{
	         pValue = (cp->m_stackItems == 0) ? m_dataStack.peek() : m_dataStack.pop();
				if (pValue != nullptr)
				{
					pVar->setValue((cp->m_stackItems == 0) ? createValue(pValue) : pValue);
               // convert to direct variable access without name lookup
		         if (vs->createVariableReferenceRestorePoint(m_cp, cp->m_operand.m_identifier))
		         {
                  cp->m_opCode = OPCODE_SET_VARPTR;
                  cp->m_operand.m_variable = pVar;
		         }
				}
				else
				{
					error(NXSL_ERR_DATA_STACK_UNDERFLOW);
				}
			}
			else
			{
				error(NXSL_ERR_ASSIGNMENT_TO_CONSTANT);
			}
         break;
      case OPCODE_SET_VARPTR:
         pValue = (cp->m_stackItems == 0) ? m_dataStack.peek() : m_dataStack.pop();
         if (pValue != nullptr)
         {
            cp->m_operand.m_variable->setValue((cp->m_stackItems == 0) ? createValue(pValue) : pValue);
         }
         else
         {
            error(NXSL_ERR_DATA_STACK_UNDERFLOW);
         }
         break;
      case OPCODE_SET_EXPRVAR:
         pValue = (cp->m_stackItems == 0) ? m_dataStack.peek() : m_dataStack.pop();
         if (pValue != nullptr)
         {
            if (m_expressionVariables == nullptr)
               m_expressionVariables = new NXSL_VariableSystem(this, NXSL_VariableSystemType::EXPRESSION);

            pVar = m_expressionVariables->find(*cp->m_operand.m_identifier);
            if (pVar != nullptr)
            {
               pVar->setValue((cp->m_stackItems == 0) ? createValue(pValue) : pValue);
            }
            else
            {
               m_expressionVariables->create(*cp->m_operand.m_identifier,
                     (cp->m_stackItems == 0) ? createValue(pValue) : pValue);
            }
         }
         else
         {
            error(NXSL_ERR_DATA_STACK_UNDERFLOW);
         }
         break;
		case OPCODE_ARRAY:
			// Check if variable already exist
			pVar = findVariable(*cp->m_operand.m_identifier);
			if (pVar != nullptr)
			{
				// only raise error if variable with given name already exist
				// and is not an array
				if (!pVar->getValue()->isArray())
				{
					error(NXSL_ERR_VARIABLE_ALREADY_EXIST);
				}
			}
			else
			{
				pVar = createVariable(*cp->m_operand.m_identifier);
				if (pVar != nullptr)
				{
					pVar->setValue(createValue(new NXSL_Array(this)));
				}
				else
				{
					error(NXSL_ERR_VARIABLE_ALREADY_EXIST);
				}
			}
			break;
		case OPCODE_GLOBAL_ARRAY:
			// Check if variable already exist
			pVar = m_globalVariables->find(*cp->m_operand.m_identifier);
			if (pVar == nullptr)
			{
				// raise error if variable with given name already exist and is not global
				if (findVariable(*cp->m_operand.m_identifier) != nullptr)
				{
					error(NXSL_ERR_VARIABLE_ALREADY_EXIST);
				}
				else
				{
					m_globalVariables->create(*cp->m_operand.m_identifier, createValue(new NXSL_Array(this)));
				}
			}
			else
			{
				if (!pVar->getValue()->isArray())
				{
					error(NXSL_ERR_VARIABLE_ALREADY_EXIST);
				}
			}
			break;
		case OPCODE_GLOBAL:
			// Check if variable already exist
			pVar = m_globalVariables->find(*cp->m_operand.m_identifier);
			if (pVar == nullptr)
			{
				// raise error if variable with given name already exist and is not global
				if (findVariable(*cp->m_operand.m_identifier) != nullptr)
				{
					error(NXSL_ERR_VARIABLE_ALREADY_EXIST);
				}
				else
				{
					if (cp->m_stackItems > 0)	// with initialization
					{
						pValue = m_dataStack.pop();
						if (pValue != nullptr)
						{
							m_globalVariables->create(*cp->m_operand.m_identifier, pValue);
						}
						else
						{
			            error(NXSL_ERR_DATA_STACK_UNDERFLOW);
						}
					}
					else
					{
						m_globalVariables->create(*cp->m_operand.m_identifier, createValue());
					}
				}
			}
         else if (cp->m_stackItems > 0)	// process initialization block as assignment
         {
            pValue = m_dataStack.pop();
            if (pValue != nullptr)
            {
               pVar->setValue(pValue);
            }
            else
            {
               error(NXSL_ERR_DATA_STACK_UNDERFLOW);
            }
         }
			break;
		case OPCODE_GET_RANGE:
		   pValue = m_dataStack.pop();
		   if (pValue != nullptr)
		   {
            NXSL_Value *start = m_dataStack.pop();
            NXSL_Value *container = m_dataStack.pop();
            if ((start != nullptr) && (container != nullptr))
            {
               if ((pValue->isInteger() || pValue->isNull()) && (start->isInteger() || start->isNull()))
               {
                  if (container->isArray())
                  {
                     NXSL_Array *src = container->getValueAsArray();
                     NXSL_Array *dst = new NXSL_Array(this);
                     int startIndex = start->isNull() ? src->getMinIndex() : start->getValueAsInt32();
                     int endIndex = pValue->isNull() ? src->getMaxIndex() + 1 : pValue->getValueAsInt32();
                     for(int i = startIndex; i < endIndex; i++)
                     {
                        NXSL_Value *v = src->get(i);
                        dst->append((v != nullptr) ? createValue(v) : createValue());
                     }
                     m_dataStack.push(createValue(dst));
                  }
                  else if (container->isString())
                  {
                     UINT32 slen;
                     const TCHAR *base = container->getValueAsString(&slen);

                     int startIndex = start->isNull() ? 0 : start->getValueAsInt32();
                     int endIndex = pValue->isNull() ? static_cast<int>(slen) : pValue->getValueAsInt32();

                     if ((startIndex >= 0) && (endIndex >= 0) && (startIndex < static_cast<int>(slen)) && (endIndex >= startIndex))
                     {
                        base += startIndex;
                        slen -= startIndex;
                        UINT32 count = static_cast<UINT32>(endIndex - startIndex);
                        if (count > slen)
                           count = slen;
                        m_dataStack.push(createValue(base, count));
                     }
                     else
                     {
                        m_dataStack.push(createValue(_T("")));
                     }
                  }
                  else
                  {
                     error(NXSL_ERR_NOT_CONTAINER);
                  }
               }
               else
               {
                  error(NXSL_ERR_NOT_INTEGER);
               }
            }
            else
            {
               error(NXSL_ERR_DATA_STACK_UNDERFLOW);
            }
            destroyValue(start);
            destroyValue(container);
            destroyValue(pValue);
		   }
		   else
		   {
            error(NXSL_ERR_DATA_STACK_UNDERFLOW);
		   }
		   break;
		case OPCODE_SET_ELEMENT:	// Set array or map element; stack should contain: array index value (top) / hashmap key value (top)
			pValue = m_dataStack.pop();
			if (pValue != nullptr)
			{
				NXSL_Value *key = m_dataStack.pop();
				NXSL_Value *container = m_dataStack.pop();
				if ((key != nullptr) && (container != nullptr))
				{
               bool success;
               if (container->isArray())
               {
                  success = setArrayElement(container, key, pValue);
               }
               else if (container->isHashMap())
               {
                  success = setHashMapElement(container, key, pValue);
               }
               else
               {
						error(NXSL_ERR_NOT_CONTAINER);
                  success = false;
               }
               if (success && (cp->m_stackItems == 0))   // Do not push value back if operation is combined with POP
               {
		            m_dataStack.push(pValue);
		            pValue = nullptr;		// Prevent deletion
               }
				}
				else
				{
					error(NXSL_ERR_DATA_STACK_UNDERFLOW);
				}
				destroyValue(key);
				destroyValue(container);
				destroyValue(pValue);
			}
         else
         {
            error(NXSL_ERR_DATA_STACK_UNDERFLOW);
         }
			break;
		case OPCODE_GET_ELEMENT:	// Get array or map element; stack should contain: array index (top) (or hashmap key (top))
		case OPCODE_INC_ELEMENT:	// Get array or map  element and increment; stack should contain: array index (top)
		case OPCODE_DEC_ELEMENT:	// Get array or map  element and decrement; stack should contain: array index (top)
		case OPCODE_INCP_ELEMENT:	// Increment array or map  element and get; stack should contain: array index (top)
		case OPCODE_DECP_ELEMENT:	// Decrement array or map  element and get; stack should contain: array index (top)
			pValue = m_dataStack.pop();
			if (pValue != nullptr)
			{
				NXSL_Value *container = m_dataStack.pop();
				if (container != nullptr)
				{
					if (container->isArray())
					{
                  getOrUpdateArrayElement(cp->m_opCode, container, pValue);
					}
               else if (container->isHashMap())
               {
                  getOrUpdateHashMapElement(cp->m_opCode, container, pValue);
               }
					else
					{
						error(NXSL_ERR_NOT_CONTAINER);
					}
					destroyValue(container);
				}
				else
				{
					error(NXSL_ERR_DATA_STACK_UNDERFLOW);
				}
				destroyValue(pValue);
			}
         else
         {
            error(NXSL_ERR_DATA_STACK_UNDERFLOW);
         }
			break;
      case OPCODE_PEEK_ELEMENT:   // Get array or map element keeping array and index on stack; stack should contain: array index (top) (or hashmap key (top))
         pValue = m_dataStack.peek();
         if (pValue != nullptr)
         {
            NXSL_Value *container = m_dataStack.peekAt(2);
            if (container != nullptr)
            {
               if (container->isArray())
               {
                  getOrUpdateArrayElement(cp->m_opCode, container, pValue);
               }
               else if (container->isHashMap())
               {
                  getOrUpdateHashMapElement(cp->m_opCode, container, pValue);
               }
               else
               {
                  error(NXSL_ERR_NOT_CONTAINER);
               }
            }
            else
            {
               error(NXSL_ERR_DATA_STACK_UNDERFLOW);
            }
         }
         else
         {
            error(NXSL_ERR_DATA_STACK_UNDERFLOW);
         }
         break;
		case OPCODE_ADD_TO_ARRAY:  // add element on stack top to array; stack should contain: array new_value (top)
         pValue = m_dataStack.pop();
         if (pValue != nullptr)
         {
            NXSL_Value *array = m_dataStack.peek();
            if (array != nullptr)
            {
               if (array->isArray())
               {
                  array->copyOnWrite();
                  int index = array->getValueAsArray()->size();
                  array->getValueAsArray()->set(index, pValue);
                  pValue = nullptr;    // Prevent deletion
               }
               else
               {
                  error(NXSL_ERR_NOT_ARRAY);
               }
            }
            else
            {
               error(NXSL_ERR_DATA_STACK_UNDERFLOW);
            }
            destroyValue(pValue);
         }
         else
         {
            error(NXSL_ERR_DATA_STACK_UNDERFLOW);
         }
         break;
		case OPCODE_HASHMAP_SET:  // set hash map entry from elements on stack top; stack should contain: hashmap key value (top)
         pValue = m_dataStack.pop();
         if (pValue != nullptr)
         {
            NXSL_Value *key = m_dataStack.pop();
            if (key != nullptr)
            {
               NXSL_Value *hashMap = m_dataStack.peek();
               if (hashMap != nullptr)
               {
                  if (hashMap->isHashMap())
                  {
                     if (key->isString())
                     {
                        hashMap->getValueAsHashMap()->set(key->getValueAsCString(), pValue);
                        pValue = nullptr;    // Prevent deletion
                     }
                     else
                     {
                        error(NXSL_ERR_KEY_NOT_STRING);
                     }
                  }
                  else
                  {
                     error(NXSL_ERR_NOT_HASHMAP);
                  }
               }
               else
               {
                  error(NXSL_ERR_DATA_STACK_UNDERFLOW);
               }
               destroyValue(key);
            }
            else
            {
               error(NXSL_ERR_DATA_STACK_UNDERFLOW);
            }
            destroyValue(pValue);
         }
         else
         {
            error(NXSL_ERR_DATA_STACK_UNDERFLOW);
         }
         break;
      case OPCODE_CAST:
         pValue = m_dataStack.peek();
         if (pValue != nullptr)
         {
            if (!pValue->convert(cp->m_stackItems))
            {
               error(NXSL_ERR_TYPE_CAST);
            }
         }
         else
         {
            error(NXSL_ERR_DATA_STACK_UNDERFLOW);
         }
         break;
		case OPCODE_NAME:
         pValue = m_dataStack.peek();
         if (pValue != nullptr)
         {
				pValue->setName(cp->m_operand.m_identifier->value);
         }
         else
         {
            error(NXSL_ERR_DATA_STACK_UNDERFLOW);
         }
			break;
      case OPCODE_POP:
         for(i = 0; i < cp->m_stackItems; i++)
            destroyValue(m_dataStack.pop());
         break;
      case OPCODE_JMP:
         dwNext = cp->m_operand.m_addr;
         break;
      case OPCODE_JZ:
      case OPCODE_JNZ:
         pValue = m_dataStack.pop();
         if (pValue != nullptr)
         {
            if (pValue->isBoolean())
            {
               if (cp->m_opCode == OPCODE_JZ ? pValue->isFalse() : pValue->isTrue())
                  dwNext = cp->m_operand.m_addr;
            }
            else
            {
               error(NXSL_ERR_BAD_CONDITION);
            }
            destroyValue(pValue);
         }
         else
         {
            error(NXSL_ERR_DATA_STACK_UNDERFLOW);
         }
         break;
      case OPCODE_JZ_PEEK:
      case OPCODE_JNZ_PEEK:
			pValue = m_dataStack.peek();
         if (pValue != nullptr)
         {
            if (pValue->isBoolean())
            {
					if (cp->m_opCode == OPCODE_JZ_PEEK ? pValue->isFalse() : pValue->isTrue())
                  dwNext = cp->m_operand.m_addr;
            }
            else if (pValue->isNull())
            {
               // If on top of the stack is nullptr convert it into integer
               pValue = m_dataStack.pop();
               destroyValue(pValue);
               m_dataStack.push(createValue(0));
               bool result = (cp->m_opCode == OPCODE_JZ_PEEK);
               if (result)
                  dwNext = cp->m_operand.m_addr;
            }
            else
            {
               error(NXSL_ERR_BAD_CONDITION);
            }
         }
         else
         {
            error(NXSL_ERR_DATA_STACK_UNDERFLOW);
         }
         break;
      case OPCODE_CALL:
         dwNext = cp->m_operand.m_addr;
         callFunction(cp->m_stackItems);
         break;
      case OPCODE_CALL_EXTERNAL:
         pFunc = m_env->findFunction(*cp->m_operand.m_identifier);
         if (pFunc != nullptr)
         {
            // convert to direct call using pointer
            cp->m_opCode = OPCODE_CALL_EXTPTR;
            destroyIdentifier(cp->m_operand.m_identifier);
            cp->m_operand.m_function = pFunc;

            if (callExternalFunction(pFunc, cp->m_stackItems))
               dwNext = m_instructionSet.size();
         }
         else
         {
            uint32_t addr = getFunctionAddress(*cp->m_operand.m_identifier);
            if (addr != INVALID_ADDRESS)
            {
               // convert to CALL
               cp->m_opCode = OPCODE_CALL;
               destroyIdentifier(cp->m_operand.m_identifier);
               cp->m_operand.m_addr = addr;

               dwNext = addr;
               callFunction(cp->m_stackItems);
            }
            else
            {
               constructor = !strncmp(cp->m_operand.m_identifier->value, "__new@", 6);
               error(constructor ? NXSL_ERR_NO_OBJECT_CONSTRUCTOR : NXSL_ERR_NO_FUNCTION);
            }
         }
         break;
      case OPCODE_CALL_EXTPTR:
         if (callExternalFunction(cp->m_operand.m_function, cp->m_stackItems))
            dwNext = m_instructionSet.size();
         break;
      case OPCODE_CALL_METHOD:
         pValue = m_dataStack.peekAt(cp->m_stackItems + 1);
         if (pValue != nullptr)
         {
            if (pValue->getDataType() == NXSL_DT_OBJECT)
            {
               NXSL_Object *object = pValue->getValueAsObject();
               if (object != nullptr)
               {
                  NXSL_Value *pResult;
                  nRet = object->getClass()->callMethod(*cp->m_operand.m_identifier, object, cp->m_stackItems,
                                                        (NXSL_Value **)m_dataStack.peekList(cp->m_stackItems),
                                                        &pResult, this);
                  if (nRet == 0)
                  {
                     for(i = 0; i < cp->m_stackItems + 1; i++)
                        destroyValue(m_dataStack.pop());
                     m_dataStack.push(pResult);
                  }
                  else if (nRet == NXSL_STOP_SCRIPT_EXECUTION)
					   {
                     m_dataStack.push(pResult);
		               dwNext = m_instructionSet.size();
					   }
					   else
                  {
                     // Execution error inside method
                     error(nRet);
                  }
               }
               else
               {
                  error(NXSL_ERR_INTERNAL);
               }
            }
            else if (pValue->getDataType() == NXSL_DT_ARRAY)
            {
               pValue->copyOnWrite();  // All array methods can cause content change
               NXSL_Array *array = pValue->getValueAsArray();
               NXSL_Value *result;
               nRet = array->callMethod(*cp->m_operand.m_identifier, cp->m_stackItems, (NXSL_Value **)m_dataStack.peekList(cp->m_stackItems), &result);
               if (nRet == 0)
               {
                  for(i = 0; i < cp->m_stackItems + 1; i++)
                     destroyValue(m_dataStack.pop());
                  m_dataStack.push(result);
               }
               else
               {
                  // Execution error inside method
                  error(nRet);
               }
            }
            else if (pValue->getDataType() == NXSL_DT_HASHMAP)
            {
               pValue->copyOnWrite();  // Some methods can cause content change
               NXSL_HashMap *hashMap = pValue->getValueAsHashMap();
               NXSL_Value *result;
               nRet = hashMap->callMethod(*cp->m_operand.m_identifier, cp->m_stackItems, (NXSL_Value **)m_dataStack.peekList(cp->m_stackItems), &result);
               if (nRet == 0)
               {
                  for(i = 0; i < cp->m_stackItems + 1; i++)
                     destroyValue(m_dataStack.pop());
                  m_dataStack.push(result);
               }
               else
               {
                  // Execution error inside method
                  error(nRet);
               }
            }
            else
            {
               error(NXSL_ERR_NOT_OBJECT);
            }
         }
         else
         {
            error(NXSL_ERR_DATA_STACK_UNDERFLOW);
         }
         break;
      case OPCODE_RET_NULL:
         m_dataStack.push(createValue());
         /* no break */
      case OPCODE_RETURN:
         if (m_subLevel > 0)
         {
            m_subLevel--;

            NXSL_VariableSystem *savedExpressionVariables = static_cast<NXSL_VariableSystem*>(m_codeStack.pop());
            if (m_expressionVariables != nullptr)
            {
               m_expressionVariables->restoreVariableReferences(&m_instructionSet);
               delete m_expressionVariables;
            }
            m_expressionVariables = savedExpressionVariables;

            NXSL_VariableSystem *savedLocals = static_cast<NXSL_VariableSystem*>(m_codeStack.pop());
            if (savedLocals != nullptr)
            {
               m_localVariables->restoreVariableReferences(&m_instructionSet);
               delete m_localVariables;
               m_localVariables = savedLocals;
            }

            dwNext = CAST_FROM_POINTER(m_codeStack.pop(), UINT32);
         }
         else
         {
            // Return from main(), terminate program
            dwNext = m_instructionSet.size();
         }
         break;
      case OPCODE_BIND:
         PositionToVarName(m_nBindPos++, varName);
         pVar = m_localVariables->find(varName);
         pValue = (pVar != nullptr) ? createValue(pVar->getValue()) : createValue();
         pVar = m_localVariables->find(*cp->m_operand.m_identifier);
         if (pVar == nullptr)
            m_localVariables->create(*cp->m_operand.m_identifier, pValue);
         else
            pVar->setValue(pValue);
         break;
      case OPCODE_PRINT:
         pValue = m_dataStack.pop();
         if (pValue != nullptr)
         {
            if (pValue->convert(NXSL_DT_STRING))
               m_env->print(pValue);
            else
               error(NXSL_ERR_TYPE_CAST);
            destroyValue(pValue);
         }
         else
         {
            error(NXSL_ERR_DATA_STACK_UNDERFLOW);
         }
         break;
      case OPCODE_EXIT:
			if (m_dataStack.getPosition() > 0)
         {
            dwNext = m_instructionSet.size();
         }
         else
         {
            error(NXSL_ERR_DATA_STACK_UNDERFLOW);
         }
         break;
      case OPCODE_ABORT:
			if (m_dataStack.getPosition() > 0)
         {
            pValue = m_dataStack.pop();
            if (pValue->isInteger())
            {
               error(pValue->getValueAsInt32());
            }
            else if (pValue->isNull())
            {
               error(NXSL_ERR_EXECUTION_ABORTED);
            }
            else
            {
               error(NXSL_ERR_NOT_INTEGER);
            }
            destroyValue(pValue);
         }
         else
         {
            error(NXSL_ERR_DATA_STACK_UNDERFLOW);
         }
         break;
      case OPCODE_ADD:
      case OPCODE_SUB:
      case OPCODE_MUL:
      case OPCODE_DIV:
      case OPCODE_REM:
      case OPCODE_CONCAT:
      case OPCODE_LIKE:
      case OPCODE_ILIKE:
      case OPCODE_MATCH:
      case OPCODE_IMATCH:
      case OPCODE_IN:
      case OPCODE_EQ:
      case OPCODE_NE:
      case OPCODE_LT:
      case OPCODE_LE:
      case OPCODE_GT:
      case OPCODE_GE:
      case OPCODE_AND:
      case OPCODE_OR:
      case OPCODE_BIT_AND:
      case OPCODE_BIT_OR:
      case OPCODE_BIT_XOR:
      case OPCODE_LSHIFT:
      case OPCODE_RSHIFT:
		case OPCODE_CASE:
      case OPCODE_CASE_CONST:
      case OPCODE_CASE_LT:
      case OPCODE_CASE_CONST_LT:
      case OPCODE_CASE_GT:
      case OPCODE_CASE_CONST_GT:
         doBinaryOperation(cp->m_opCode);
         break;
      case OPCODE_NEG:
      case OPCODE_NOT:
      case OPCODE_BIT_NOT:
         doUnaryOperation(cp->m_opCode);
         break;
      case OPCODE_INC:  // Post increment/decrement
      case OPCODE_DEC:
         pVar = findOrCreateVariable(*cp->m_operand.m_identifier, &vs);
         if (!pVar->isConstant())
         {
            pValue = pVar->getValue();
            if (pValue->isNumeric())
            {
               m_dataStack.push(createValue(pValue));
               if (cp->m_opCode == OPCODE_INC)
                  pValue->increment();
               else
                  pValue->decrement();

               // Convert to direct variable access
               if (vs->createVariableReferenceRestorePoint(m_cp, cp->m_operand.m_identifier))
               {
                  cp->m_opCode = (cp->m_opCode == OPCODE_INC) ? OPCODE_INC_VARPTR : OPCODE_DEC_VARPTR;
                  cp->m_operand.m_variable = pVar;
               }
            }
            else
            {
               error(NXSL_ERR_NOT_NUMBER);
            }
         }
         else
         {
            error(NXSL_ERR_ASSIGNMENT_TO_CONSTANT);
         }
         break;
      case OPCODE_INC_VARPTR:  // Post increment/decrement
      case OPCODE_DEC_VARPTR:
         pValue = cp->m_operand.m_variable->getValue();
         if (pValue->isNumeric())
         {
            m_dataStack.push(createValue(pValue));
            if (cp->m_opCode == OPCODE_INC_VARPTR)
               pValue->increment();
            else
               pValue->decrement();
         }
         else
         {
            error(NXSL_ERR_NOT_NUMBER);
         }
         break;
      case OPCODE_INCP: // Pre increment/decrement
      case OPCODE_DECP:
         pVar = findOrCreateVariable(*cp->m_operand.m_identifier, &vs);
         if (!pVar->isConstant())
         {
            pValue = pVar->getValue();
            if (pValue->isNumeric())
            {
               if (cp->m_opCode == OPCODE_INCP)
                  pValue->increment();
               else
                  pValue->decrement();
               m_dataStack.push(createValue(pValue));

               // Convert to direct variable access
               if (vs->createVariableReferenceRestorePoint(m_cp, cp->m_operand.m_identifier))
               {
                  cp->m_opCode = (cp->m_opCode == OPCODE_INCP) ? OPCODE_INCP_VARPTR : OPCODE_DECP_VARPTR;
                  cp->m_operand.m_variable = pVar;
               }
            }
            else
            {
               error(NXSL_ERR_NOT_NUMBER);
            }
         }
         else
         {
            error(NXSL_ERR_ASSIGNMENT_TO_CONSTANT);
         }
         break;
      case OPCODE_INCP_VARPTR: // Pre increment/decrement
      case OPCODE_DECP_VARPTR:
         pValue = cp->m_operand.m_variable->getValue();
         if (pValue->isNumeric())
         {
            if (cp->m_opCode == OPCODE_INCP_VARPTR)
               pValue->increment();
            else
               pValue->decrement();
            m_dataStack.push(createValue(pValue));
         }
         else
         {
            error(NXSL_ERR_NOT_NUMBER);
         }
         break;
      case OPCODE_GET_ATTRIBUTE:
		case OPCODE_SAFE_GET_ATTR:
         pValue = m_dataStack.pop();
         if (pValue != nullptr)
         {
            if (pValue->getDataType() == NXSL_DT_OBJECT)
            {
               NXSL_Object *pObj;
               NXSL_Value *pAttr;

               pObj = pValue->getValueAsObject();
               if (pObj != nullptr)
               {
                  pAttr = pObj->getClass()->getAttr(pObj, cp->m_operand.m_identifier->value);
                  if (pAttr != nullptr)
                  {
                     m_dataStack.push(pAttr);
                  }
                  else
                  {
							if (cp->m_opCode == OPCODE_SAFE_GET_ATTR)
							{
	                     m_dataStack.push(createValue());
							}
							else
							{
								error(NXSL_ERR_NO_SUCH_ATTRIBUTE);
							}
                  }
               }
               else
               {
                  error(NXSL_ERR_INTERNAL);
               }
            }
            else if (pValue->getDataType() == NXSL_DT_ARRAY)
            {
               getArrayAttribute(pValue->getValueAsArray(), cp->m_operand.m_identifier->value, cp->m_opCode == OPCODE_SAFE_GET_ATTR);
            }
            else if (pValue->getDataType() == NXSL_DT_HASHMAP)
            {
               getHashMapAttribute(pValue->getValueAsHashMap(), cp->m_operand.m_identifier->value, cp->m_opCode == OPCODE_SAFE_GET_ATTR);
            }
            else
            {
               error(NXSL_ERR_NOT_OBJECT);
            }
            destroyValue(pValue);
         }
         else
         {
            error(NXSL_ERR_DATA_STACK_UNDERFLOW);
         }
         break;
      case OPCODE_SET_ATTRIBUTE:
         pValue = m_dataStack.pop();
         if (pValue != nullptr)
         {
				NXSL_Value *pReference = m_dataStack.pop();
				if (pReference != nullptr)
				{
					if (pReference->getDataType() == NXSL_DT_OBJECT)
					{
						NXSL_Object *pObj = pReference->getValueAsObject();
						if (pObj != nullptr)
						{
							if (pObj->getClass()->setAttr(pObj, cp->m_operand.m_identifier->value, pValue))
							{
								m_dataStack.push(pValue);
								pValue = nullptr;
							}
							else
							{
								error(NXSL_ERR_NO_SUCH_ATTRIBUTE);
							}
						}
						else
						{
							error(NXSL_ERR_INTERNAL);
						}
					}
					else
					{
						error(NXSL_ERR_NOT_OBJECT);
					}
					destroyValue(pReference);
				}
				else
				{
					error(NXSL_ERR_DATA_STACK_UNDERFLOW);
				}
				destroyValue(pValue);
         }
         else
         {
            error(NXSL_ERR_DATA_STACK_UNDERFLOW);
         }
         break;
		case OPCODE_FOREACH:
			nRet = NXSL_Iterator::createIterator(this, &m_dataStack);
			if (nRet != 0)
			{
				error(nRet);
			}
			break;
		case OPCODE_NEXT:
			pValue = m_dataStack.peek();
			if (pValue != nullptr)
			{
				if (pValue->isIterator())
				{
					NXSL_Iterator *it = pValue->getValueAsIterator();
					NXSL_Value *next = it->next();
					m_dataStack.push(createValue((LONG)((next != nullptr) ? 1 : 0)));
					NXSL_Variable *var = findOrCreateVariable(it->getVariableName());
					if (!var->isConstant())
					{
						var->setValue((next != nullptr) ? createValue(next) : createValue());
					}
					else
					{
						error(NXSL_ERR_ASSIGNMENT_TO_CONSTANT);
					}
				}
				else
				{
	            error(NXSL_ERR_NOT_ITERATOR);
				}
			}
			else
			{
            error(NXSL_ERR_DATA_STACK_UNDERFLOW);
			}
			break;
      case OPCODE_CATCH:
         {
            NXSL_CatchPoint *p = new NXSL_CatchPoint;
            p->addr = cp->m_operand.m_addr;
            p->dataStackSize = m_dataStack.getPosition();
            p->subLevel = m_subLevel;
            m_catchStack.push(p);
         }
         break;
      case OPCODE_CPOP:
         delete m_catchStack.pop();
         break;
      case OPCODE_STORAGE_WRITE:   // Write to storage; stack should contain: name value (top)
         pValue = m_dataStack.pop();
         if (pValue != nullptr)
         {
            NXSL_Value *name = m_dataStack.pop();
            if (name != nullptr)
            {
               if (name->isString())
               {
                  m_storage->write(name->getValueAsCString(), createValue(pValue));
                  m_dataStack.push(pValue);
                  pValue = nullptr;    // Prevent deletion
               }
               else
               {
                  error(NXSL_ERR_NOT_STRING);
               }
               destroyValue(name);
            }
            else
            {
               error(NXSL_ERR_DATA_STACK_UNDERFLOW);
            }
            destroyValue(pValue);
         }
         else
         {
            error(NXSL_ERR_DATA_STACK_UNDERFLOW);
         }
         break;
      case OPCODE_STORAGE_READ:   // Read from storage; stack should contain item name on top
         pValue = (cp->m_stackItems > 0) ? m_dataStack.peek() : m_dataStack.pop();
         if (pValue != nullptr)
         {
            if (pValue->isString())
            {
               m_dataStack.push(m_storage->read(pValue->getValueAsCString(), this));
            }
            else
            {
               error(NXSL_ERR_NOT_STRING);
            }
            if (cp->m_stackItems == 0)
               destroyValue(pValue);
         }
         else
         {
            error(NXSL_ERR_DATA_STACK_UNDERFLOW);
         }
         break;
      case OPCODE_STORAGE_INC:  // Post increment/decrement for storage item
      case OPCODE_STORAGE_DEC:
         pValue = m_dataStack.pop();
         if (pValue != nullptr)
         {
            if (pValue->isString())
            {
               NXSL_Value *sval = m_storage->read(pValue->getValueAsCString(), this);
               if (sval->isNumeric())
               {
                  m_dataStack.push(createValue(sval));
                  if (cp->m_opCode == OPCODE_STORAGE_INC)
                     sval->increment();
                  else
                     sval->decrement();
                  m_storage->write(pValue->getValueAsCString(), sval);
               }
               else
               {
                  error(NXSL_ERR_NOT_NUMBER);
                  destroyValue(sval);
               }
            }
            else
            {
               error(NXSL_ERR_NOT_STRING);
            }
            destroyValue(pValue);
         }
         else
         {
            error(NXSL_ERR_DATA_STACK_UNDERFLOW);
         }
         break;
      case OPCODE_STORAGE_INCP: // Pre increment/decrement for storage item
      case OPCODE_STORAGE_DECP:
         pValue = m_dataStack.pop();
         if (pValue != nullptr)
         {
            if (pValue->isString())
            {
               NXSL_Value *sval = m_storage->read(pValue->getValueAsCString(), this);
               if (sval->isNumeric())
               {
                  if (cp->m_opCode == OPCODE_STORAGE_INCP)
                     sval->increment();
                  else
                     sval->decrement();
                  m_dataStack.push(createValue(sval));
                  m_storage->write(pValue->getValueAsCString(), sval);
               }
               else
               {
                  error(NXSL_ERR_NOT_NUMBER);
                  destroyValue(sval);
               }
            }
            else
            {
               error(NXSL_ERR_NOT_STRING);
            }
            destroyValue(pValue);
         }
         else
         {
            error(NXSL_ERR_DATA_STACK_UNDERFLOW);
         }
         break;
      case OPCODE_PUSHCP:
         m_dataStack.push(createValue((INT32)m_cp + cp->m_stackItems));
         break;
      case OPCODE_SELECT:
         dwNext = callSelector(*cp->m_operand.m_identifier, cp->m_stackItems);
         break;
      default:
         break;
   }

   if (m_cp != INVALID_ADDRESS)
      m_cp = dwNext;
}

/**
 * Set array element
 */
bool NXSL_VM::setArrayElement(NXSL_Value *array, NXSL_Value *index, NXSL_Value *value)
{
   bool success;
	if (index->isInteger())
	{
      // copy on write
      array->copyOnWrite();
		array->getValueAsArray()->set(index->getValueAsInt32(), createValue(value));
      success = true;
	}
   else
	{
		error(NXSL_ERR_INDEX_NOT_INTEGER);
      success = false;
	}
   return success;
}

/**
 * Get or update array element
 */
void NXSL_VM::getOrUpdateArrayElement(int opcode, NXSL_Value *array, NXSL_Value *index)
{
	if (index->isInteger())
	{
      if ((opcode != OPCODE_GET_ELEMENT) && (opcode != OPCODE_PEEK_ELEMENT))
         array->copyOnWrite();
		NXSL_Value *element = array->getValueAsArray()->get(index->getValueAsInt32());

      if (opcode == OPCODE_INCP_ELEMENT)
      {
         if ((element != nullptr) && element->isNumeric())
         {
            element->increment();
         }
         else
         {
            error(NXSL_ERR_NOT_NUMBER);
         }
      }
      else if (opcode == OPCODE_DECP_ELEMENT)
      {
         if ((element != nullptr) && element->isNumeric())
         {
            element->decrement();
         }
         else
         {
            error(NXSL_ERR_NOT_NUMBER);
         }
      }

      m_dataStack.push((element != nullptr) ? createValue(element) : createValue());

      if (opcode == OPCODE_INC_ELEMENT)
      {
         if ((element != nullptr) && element->isNumeric())
         {
            element->increment();
         }
         else
         {
            error(NXSL_ERR_NOT_NUMBER);
         }
      }
      else if (opcode == OPCODE_DEC_ELEMENT)
      {
         if ((element != nullptr) && element->isNumeric())
         {
            element->decrement();
         }
         else
         {
            error(NXSL_ERR_NOT_NUMBER);
         }
      }
	}
	else
	{
		error(NXSL_ERR_INDEX_NOT_INTEGER);
	}
}

/**
 * Set hash map element
 */
bool NXSL_VM::setHashMapElement(NXSL_Value *hashMap, NXSL_Value *key, NXSL_Value *value)
{
   bool success;
	if (key->isString())
	{
      hashMap->copyOnWrite();
		hashMap->getValueAsHashMap()->set(key->getValueAsCString(), createValue(value));
      success = true;
	}
   else
	{
		error(NXSL_ERR_KEY_NOT_STRING);
      success = false;
	}
   return success;
}

/**
 * Get or update hash map element
 */
void NXSL_VM::getOrUpdateHashMapElement(int opcode, NXSL_Value *hashMap, NXSL_Value *key)
{
	if (!key->isString())
   {
      error(NXSL_ERR_KEY_NOT_STRING);
      return;
   }

   NXSL_Value *element = hashMap->getValueAsHashMap()->get(key->getValueAsCString());
   if (element == nullptr)
   {
      error(NXSL_ERR_NULL_VALUE);
      return;
   }

   if ((opcode != OPCODE_GET_ELEMENT) && (opcode != OPCODE_PEEK_ELEMENT))
      hashMap->copyOnWrite();

   if (opcode == OPCODE_INCP_ELEMENT)
   {
      if (element->isNumeric())
      {
         element->increment();
      }
      else
      {
         error(NXSL_ERR_NOT_NUMBER);
      }
   }
   else if (opcode == OPCODE_DECP_ELEMENT)
   {
      if (element->isNumeric())
      {
         element->decrement();
      }
      else
      {
         error(NXSL_ERR_NOT_NUMBER);
      }
   }

   m_dataStack.push((element != nullptr) ? createValue(element) : createValue());

   if (opcode == OPCODE_INC_ELEMENT)
   {
      if (element->isNumeric())
      {
         element->increment();
      }
      else
      {
         error(NXSL_ERR_NOT_NUMBER);
      }
   }
   else if (opcode == OPCODE_DEC_ELEMENT)
   {
      if (element->isNumeric())
      {
         element->decrement();
      }
      else
      {
         error(NXSL_ERR_NOT_NUMBER);
      }
   }
}

/**
 * Perform binary operation on two operands from stack and push result to stack
 */
void NXSL_VM::doBinaryOperation(int nOpCode)
{
   NXSL_Value *pVal1, *pVal2, *pRes = nullptr;
   NXSL_Variable *var;
   const TCHAR *pszText1, *pszText2;
   UINT32 dwLen1, dwLen2;
   int nType;
   bool bResult;
   bool dynamicValues = false;

   switch(nOpCode)
   {
      case OPCODE_CASE:
      case OPCODE_CASE_LT:
      case OPCODE_CASE_GT:
		   pVal1 = m_instructionSet.get(m_cp)->m_operand.m_constant;
		   pVal2 = m_dataStack.peek();
         break;
      case OPCODE_CASE_CONST:
      case OPCODE_CASE_CONST_LT:
      case OPCODE_CASE_CONST_GT:
         pVal1 = m_env->getConstantValue(*(m_instructionSet.get(m_cp)->m_operand.m_identifier), this);
         if (pVal1 == nullptr)
         {
            var = (m_constants != nullptr) ? m_constants->find(*(m_instructionSet.get(m_cp)->m_operand.m_identifier)) : nullptr;
            if (var != nullptr)
            {
               pVal1 = var->getValue();
            }
            else
            {
               error(NXSL_ERR_NO_SUCH_CONSTANT);
               return;
            }
         }
		   pVal2 = m_dataStack.peek();
         break;
      default:
		   pVal2 = m_dataStack.pop();
		   pVal1 = m_dataStack.pop();
         dynamicValues = true;
         break;
   }

   if ((pVal1 != nullptr) && (pVal2 != nullptr))
   {
      if ((!pVal1->isNull() && !pVal2->isNull()) ||
          (!pVal2->isNull() && (nOpCode == OPCODE_IN)) ||
          (nOpCode == OPCODE_EQ) || (nOpCode == OPCODE_NE) || (nOpCode == OPCODE_CASE) ||
          (nOpCode == OPCODE_CASE_CONST) || (nOpCode == OPCODE_CONCAT) ||
          (nOpCode == OPCODE_AND) || (nOpCode == OPCODE_OR) ||
          (nOpCode == OPCODE_CASE_LT) || (nOpCode == OPCODE_CASE_CONST_LT) ||
          (nOpCode == OPCODE_CASE_GT) || (nOpCode == OPCODE_CASE_CONST_GT))
      {
         if (pVal1->isNumeric() && pVal2->isNumeric() &&
             (nOpCode != OPCODE_CONCAT) && (nOpCode != OPCODE_IN) &&
             (nOpCode != OPCODE_LIKE) && (nOpCode != OPCODE_ILIKE) &&
             (nOpCode != OPCODE_MATCH) && (nOpCode != OPCODE_IMATCH))
         {
            nType = SelectResultType(pVal1->getDataType(), pVal2->getDataType(), nOpCode);
            if (nType != NXSL_DT_NULL)
            {
               if ((pVal1->convert(nType)) && (pVal2->convert(nType)))
               {
                  switch(nOpCode)
                  {
                     case OPCODE_ADD:
                        pRes = pVal1;
                        pRes->add(pVal2);
                        pVal1 = nullptr;
                        break;
                     case OPCODE_SUB:
                        pRes = pVal1;
                        pRes->sub(pVal2);
                        pVal1 = nullptr;
                        break;
                     case OPCODE_MUL:
                        pRes = pVal1;
                        pRes->mul(pVal2);
                        pVal1 = nullptr;
                        break;
                     case OPCODE_DIV:
                        pRes = pVal1;
                        pRes->div(pVal2);
                        pVal1 = nullptr;
                        break;
                     case OPCODE_REM:
                        pRes = pVal1;
                        pRes->rem(pVal2);
                        pVal1 = nullptr;
                        break;
                     case OPCODE_EQ:
                     case OPCODE_NE:
                        pRes = createValue((nOpCode == OPCODE_EQ) ? pVal1->EQ(pVal2) : !pVal1->EQ(pVal2));
                        break;
                     case OPCODE_LT:
                        pRes = createValue(pVal1->LT(pVal2));
                        break;
                     case OPCODE_LE:
                        pRes = createValue(pVal1->LE(pVal2));
                        break;
                     case OPCODE_GT:
                        pRes = createValue(pVal1->GT(pVal2));
                        break;
                     case OPCODE_GE:
                        pRes = createValue(pVal1->GE(pVal2));
                        break;
                     case OPCODE_LSHIFT:
                        pRes = pVal1;
                        pRes->lshift(pVal2->getValueAsInt32());
                        pVal1 = nullptr;
                        break;
                     case OPCODE_RSHIFT:
                        pRes = pVal1;
                        pRes->rshift(pVal2->getValueAsInt32());
                        pVal1 = nullptr;
                        break;
                     case OPCODE_BIT_AND:
                        pRes = pVal1;
                        pRes->bitAnd(pVal2);
                        pVal1 = nullptr;
                        break;
                     case OPCODE_BIT_OR:
                        pRes = pVal1;
                        pRes->bitOr(pVal2);
                        pVal1 = nullptr;
                        break;
                     case OPCODE_BIT_XOR:
                        pRes = pVal1;
                        pRes->bitXor(pVal2);
                        pVal1 = nullptr;
                        break;
                     case OPCODE_AND:
                        pRes = createValue(pVal1->isTrue() && pVal2->isTrue());
                        break;
                     case OPCODE_OR:
                        pRes = createValue(pVal1->isTrue() || pVal2->isTrue());
                        break;
                     case OPCODE_CASE:
                     case OPCODE_CASE_CONST:
                        pRes = createValue((LONG)pVal1->EQ(pVal2));
                        break;
                     case OPCODE_CASE_LT:    // val2 is switch value, val1 is check value
                     case OPCODE_CASE_CONST_LT:
                        pRes = createValue((LONG)pVal2->LT(pVal1));
                        break;
                     case OPCODE_CASE_GT:    // val2 is switch value, val1 is check value
                     case OPCODE_CASE_CONST_GT:
                        pRes = createValue((LONG)pVal2->GT(pVal1));
                        break;
                     default:
                        error(NXSL_ERR_INTERNAL);
                        break;
                  }
               }
               else
               {
                  error(NXSL_ERR_TYPE_CAST);
               }
            }
            else
            {
               error(NXSL_ERR_REAL_VALUE);
            }
         }
         else if (((nOpCode == OPCODE_AND) || (nOpCode == OPCODE_OR)) && pVal1->isBoolean() && pVal2->isBoolean())
         {
            bool result = (nOpCode == OPCODE_AND) ? (pVal1->isTrue() && pVal2->isTrue()) : (pVal1->isTrue() || pVal2->isTrue());
            pRes = createValue(result);
         }
         else
         {
            switch(nOpCode)
            {
               case OPCODE_EQ:
               case OPCODE_NE:
					case OPCODE_CASE:
					case OPCODE_CASE_CONST:
                  if (pVal1->isNull() && pVal2->isNull())
                  {
                     bResult = true;
                  }
                  else if (pVal1->isNull() || pVal2->isNull())
                  {
                     bResult = false;
                  }
                  else
                  {
                     pszText1 = pVal1->getValueAsString(&dwLen1);
                     pszText2 = pVal2->getValueAsString(&dwLen2);
                     if (dwLen1 == dwLen2)
                        bResult = (memcmp(pszText1, pszText2, dwLen1 * sizeof(TCHAR)) == 0);
                     else
                        bResult = false;
                  }
                  pRes = createValue((nOpCode == OPCODE_NE) ? !bResult : bResult);
                  break;
               case OPCODE_CONCAT:
                  pRes = pVal1;
                  pVal1 = nullptr;
                  if (pRes->convert(NXSL_DT_STRING))
                  {
                     pszText2 = pVal2->getValueAsString(&dwLen2);
                     pRes->concatenate(pszText2, dwLen2);
                  }
                  else
                  {
                     error(NXSL_ERR_TYPE_CAST);
                  }
                  break;
               case OPCODE_LIKE:
               case OPCODE_ILIKE:
                  if (pVal1->isString() && pVal2->isString())
                  {
                     pRes = createValue(MatchString(pVal2->getValueAsCString(), pVal1->getValueAsCString(), nOpCode == OPCODE_LIKE));
                  }
                  else
                  {
                     error(NXSL_ERR_NOT_STRING);
                  }
                  break;
               case OPCODE_MATCH:
               case OPCODE_IMATCH:
                  if (pVal1->isString() && pVal2->isString())
                  {
                     pRes = matchRegexp(pVal1, pVal2, nOpCode == OPCODE_IMATCH);
                  }
                  else
                  {
                     error(NXSL_ERR_NOT_STRING);
                  }
                  break;
               case OPCODE_IN:
                  if (pVal2->isArray())
                  {
                     pRes = createValue(pVal2->getValueAsArray()->contains(pVal1));
                  }
                  else if (pVal2->isHashMap())
                  {
                     if (pVal1->isString())
                        pRes = createValue(pVal2->getValueAsHashMap()->contains(pVal1->getValueAsCString()));
                     else
                        error(NXSL_ERR_NOT_STRING);
                  }
                  else if (pVal2->isString())
                  {
                     if (pVal1->isString())
                     {
                        uint32_t len1, len2;
                        const TCHAR *s1 = pVal1->getValueAsString(&len1);
                        const TCHAR *s2 = pVal2->getValueAsString(&len2);
                        pRes = createValue((len1 <= len2) && (memmem(s2, len2 * sizeof(TCHAR), s1, len1 * sizeof(TCHAR)) != nullptr));
                     }
                     else
                     {
                        error(NXSL_ERR_NOT_STRING);
                     }
                  }
                  else
                  {
                     error(NXSL_ERR_NOT_CONTAINER);
                  }
                  break;
               case OPCODE_ADD:
               case OPCODE_SUB:
               case OPCODE_MUL:
               case OPCODE_DIV:
               case OPCODE_REM:
               case OPCODE_LT:
               case OPCODE_LE:
               case OPCODE_GT:
               case OPCODE_GE:
               case OPCODE_AND:
               case OPCODE_OR:
               case OPCODE_BIT_AND:
               case OPCODE_BIT_OR:
               case OPCODE_BIT_XOR:
               case OPCODE_LSHIFT:
               case OPCODE_RSHIFT:
               case OPCODE_CASE_LT:
               case OPCODE_CASE_CONST_LT:
               case OPCODE_CASE_GT:
               case OPCODE_CASE_CONST_GT:
                  error(NXSL_ERR_NOT_NUMBER);
                  break;
               default:
                  error(NXSL_ERR_INTERNAL);
                  break;
            }
         }
      }
      else
      {
         error(NXSL_ERR_NULL_VALUE);
      }
   }
   else
   {
      error(NXSL_ERR_DATA_STACK_UNDERFLOW);
   }

   if (dynamicValues)
   {
      destroyValue(pVal1);
      destroyValue(pVal2);
   }

   if (pRes != nullptr)
      m_dataStack.push(pRes);
}

/**
 * Perform unary operation on operand from the stack and push result back to stack
 */
void NXSL_VM::doUnaryOperation(int nOpCode)
{
   NXSL_Value *value = m_dataStack.peek();
   if (value != nullptr)
   {
      if ((nOpCode == OPCODE_NOT) && value->isBoolean())
      {
         value->set(value->isFalse());
      }
      else if (value->isNumeric())
      {
         switch(nOpCode)
         {
            case OPCODE_BIT_NOT:
               if (!value->isReal())
               {
                  value->bitNot();
               }
               else
               {
                  error(NXSL_ERR_REAL_VALUE);
               }
               break;
            case OPCODE_NEG:
               value->negate();
               break;
            case OPCODE_NOT:
               value->set(value->isFalse());
               break;
            default:
               error(NXSL_ERR_INTERNAL);
               break;
         }
      }
      else
      {
         error((nOpCode == OPCODE_NOT) ? NXSL_ERR_NOT_BOOLEAN : NXSL_ERR_NOT_NUMBER);
      }
   }
   else
   {
      error(NXSL_ERR_DATA_STACK_UNDERFLOW);
   }
}

/**
 * Relocate code block
 */
void NXSL_VM::relocateCode(uint32_t start, uint32_t len, uint32_t shift)
{
   uint32_t last = std::min(start + len, static_cast<uint32_t>(m_instructionSet.size()));
   for(uint32_t i = start; i < last; i++)
	{
      NXSL_Instruction *instr = m_instructionSet.get(i);
      if ((instr->m_opCode == OPCODE_JMP) ||
          (instr->m_opCode == OPCODE_JZ) ||
          (instr->m_opCode == OPCODE_JNZ) ||
          (instr->m_opCode == OPCODE_JZ_PEEK) ||
          (instr->m_opCode == OPCODE_JNZ_PEEK) ||
          (instr->m_opCode == OPCODE_CALL))
      {
         instr->m_operand.m_addr += shift;
      }
	}
}

/**
 * Use external module
 */
void NXSL_VM::loadModule(NXSL_Program *module, const NXSL_ModuleImport *importInfo)
{
   // Check if module already loaded
   for(int i = 0; i < m_modules.size(); i++)
      if (!_tcsicmp(importInfo->name, m_modules.get(i)->m_name))
         return;  // Already loaded

   // Add code from module
   int start = m_instructionSet.size();
   for(int i = 0; i < module->m_instructionSet.size(); i++)
      m_instructionSet.addPlaceholder()->copyFrom(module->m_instructionSet.get(i), this);
   relocateCode(start, module->m_instructionSet.size(), start);

   // Add function names from module
   int fnstart = m_functions.size();
   char fname[MAX_IDENTIFIER_LENGTH];
#ifdef UNICODE
   wchar_to_utf8(importInfo->name, -1, fname, MAX_IDENTIFIER_LENGTH - 1);
   fname[MAX_IDENTIFIER_LENGTH - 1] = 0;
#else
   strlcpy(fname, importInfo->name, MAX_IDENTIFIER_LENGTH);
#endif
   strlcat(fname, "::", MAX_IDENTIFIER_LENGTH);
   size_t fnpos = strlen(fname);
   for(int i = 0; i < module->m_functions.size(); i++)
   {
      NXSL_Function *mf = module->m_functions.get(i);
      if (mf->m_name.length < MAX_IDENTIFIER_LENGTH - fnpos)
      {
         // Add fully qualified function name (module::function)
         strcpy(&fname[fnpos], mf->m_name.value);
         m_functions.add(NXSL_Function(fname, mf->m_addr + start));
      }
      if (!strcmp(mf->m_name.value, "main") || !strcmp(mf->m_name.value, "$main"))
         continue;
      NXSL_Function f(mf);
      f.m_addr += static_cast<uint32_t>(start);
      m_functions.add(f);
   }

   // Add constants from module
   if (module->m_constants.size() > 0)
   {
      if (m_constants == nullptr)
         m_constants = new NXSL_VariableSystem(this, NXSL_VariableSystemType::CONSTANT);
      m_constants->addAll(module->m_constants);
   }

   // Register module as loaded
   NXSL_Module *m = new NXSL_Module;
   _tcslcpy(m->m_name, importInfo->name, MAX_PATH);
   m->m_codeStart = (UINT32)start;
   m->m_codeSize = module->m_instructionSet.size();
   m->m_functionStart = fnstart;
   m->m_numFunctions = m_functions.size() - fnstart;
   m_modules.add(m);
}

/**
 * Call external function
 */
bool NXSL_VM::callExternalFunction(const NXSL_ExtFunction *function, int stackItems)
{
   bool stopExecution = false;
   bool constructor = !strncmp(function->m_name, "__new@", 6);
   if ((stackItems == function->m_iNumArgs) || (function->m_iNumArgs == -1))
   {
      if (m_dataStack.getPosition() >= stackItems)
      {
         NXSL_Value *result = nullptr;
         int ret = function->m_pfHandler(stackItems,
                  (NXSL_Value **)m_dataStack.peekList(stackItems), &result, this);
         if (ret == 0)
         {
            for(int i = 0; i < stackItems; i++)
               destroyValue(m_dataStack.pop());
            m_dataStack.push(result);
         }
         else if (ret == NXSL_STOP_SCRIPT_EXECUTION)
         {
            m_dataStack.push(result);
            stopExecution = true;
         }
         else
         {
            // Execution error inside function
            error(ret);
         }
      }
      else
      {
         error(NXSL_ERR_DATA_STACK_UNDERFLOW);
      }
   }
   else
   {
      error(constructor ? NXSL_ERR_INVALID_OC_ARG_COUNT : NXSL_ERR_INVALID_ARGUMENT_COUNT);
   }
   return stopExecution;
}

/**
 * Call function at given address
 */
void NXSL_VM::callFunction(int nArgCount)
{
   if (m_subLevel < CONTROL_STACK_LIMIT)
   {
      m_subLevel++;
      m_codeStack.push(CAST_TO_POINTER(m_cp + 1, void *));
      m_codeStack.push(m_localVariables);
      m_localVariables->restoreVariableReferences(&m_instructionSet);
      m_localVariables = new NXSL_VariableSystem(this, NXSL_VariableSystemType::LOCAL);
      m_codeStack.push(m_expressionVariables);
      if (m_expressionVariables != nullptr)
      {
         m_expressionVariables->restoreVariableReferences(&m_instructionSet);
         m_expressionVariables = nullptr;
      }
      m_nBindPos = 1;

      // Bind arguments
      for(int i = nArgCount; i > 0; i--)
      {
         NXSL_Value *pValue = m_dataStack.pop();
         if (pValue != nullptr)
         {
            char varName[MAX_IDENTIFIER_LENGTH];
            PositionToVarName(i, varName);
            m_localVariables->create(varName, pValue);
				if (pValue->getName() != nullptr)
				{
					// Named parameter
				   strlcpy(&varName[1], pValue->getName(), MAX_IDENTIFIER_LENGTH - 1);
	            m_localVariables->create(varName, createValue(pValue));
				}
         }
         else
         {
            error(NXSL_ERR_DATA_STACK_UNDERFLOW);
            break;
         }
      }
   }
   else
   {
      error(NXSL_ERR_CONTROL_STACK_OVERFLOW);
   }
}

/**
 * Find function address by name
 */
uint32_t NXSL_VM::getFunctionAddress(const NXSL_Identifier& name)
{
   for(int i = 0; i < m_functions.size(); i++)
   {
      NXSL_Function *f = m_functions.get(i);
      if (name.equals(f->m_name))
         return f->m_addr;
   }
   return INVALID_ADDRESS;
}

/**
 * Call selector
 */
UINT32 NXSL_VM::callSelector(const NXSL_Identifier& name, int numElements)
{
   const NXSL_ExtSelector *selector = m_env->findSelector(name);
   if (selector == nullptr)
   {
      error(NXSL_ERR_NO_SELECTOR);
      return 0;
   }

   int err, selection = -1;
   uint32_t addr = 0;
   NXSL_Value *options = nullptr;
   uint32_t *addrList = static_cast<uint32_t*>(MemAllocLocal(sizeof(uint32_t) * numElements));
   NXSL_Value **valueList = static_cast<NXSL_Value**>(MemAllocLocal(sizeof(NXSL_Value *) * numElements));
   memset(valueList, 0, sizeof(NXSL_Value *) * numElements);

   for(int i = numElements - 1; i >= 0; i--)
   {
      NXSL_Value *v = m_dataStack.pop();
      if (v == nullptr)
      {
         error(NXSL_ERR_DATA_STACK_UNDERFLOW);
         goto cleanup;
      }

      if (!v->isInteger())
      {
         destroyValue(v);
         error(NXSL_ERR_INTERNAL);
         goto cleanup;
      }
      addrList[i] = v->getValueAsUInt32();
      destroyValue(v);

      valueList[i] = m_dataStack.pop();
      if (valueList[i] == nullptr)
      {
         error(NXSL_ERR_DATA_STACK_UNDERFLOW);
         goto cleanup;
      }
   }

   options = m_dataStack.pop();
   if (options == nullptr)
   {
      error(NXSL_ERR_DATA_STACK_UNDERFLOW);
      goto cleanup;
   }

   err = selector->m_handler(name, options, numElements, valueList, &selection, this);
   if (err == NXSL_ERR_SUCCESS)
   {
      if ((selection >= 0) && (selection < numElements))
      {
         addr = addrList[selection];
      }
      else
      {
         addr = m_cp + 1;
      }
   }
   else
   {
      error(err);
   }

cleanup:
   for(int j = 0; j < numElements; j++)
      destroyValue(valueList[j]);
   destroyValue(options);

   MemFreeLocal(addrList);
   MemFreeLocal(valueList);

   return addr;
}

/**
 * Max number of capture groups in regular expression
 */
#define MAX_REGEXP_CGROUPS    64

/**
 * Match regular expression
 */
NXSL_Value *NXSL_VM::matchRegexp(NXSL_Value *value, NXSL_Value *regexp, bool ignoreCase)
{
   NXSL_Value *result;

   const TCHAR *re = regexp->getValueAsCString();
   const char *eptr;
   int eoffset;
	PCRE *preg = _pcre_compile_t(reinterpret_cast<const PCRE_TCHAR*>(re), ignoreCase ? PCRE_COMMON_FLAGS | PCRE_CASELESS : PCRE_COMMON_FLAGS, &eptr, &eoffset, nullptr);
   if (preg != nullptr)
   {
      int pmatch[MAX_REGEXP_CGROUPS * 3];
      UINT32 valueLen;
		const TCHAR *v = value->getValueAsString(&valueLen);
		int cgcount = _pcre_exec_t(preg, nullptr, reinterpret_cast<const PCRE_TCHAR*>(v), valueLen, 0, 0, pmatch, MAX_REGEXP_CGROUPS * 3);
      if (cgcount >= 0)
      {
         if (cgcount == 0)
            cgcount = MAX_REGEXP_CGROUPS;

         NXSL_Array *cgroups = new NXSL_Array(this);
         int i;
         for(i = 0; i < cgcount; i++)
         {
            char varName[16];
            PositionToVarName(i, varName);
            NXSL_Variable *var = m_localVariables->find(varName);

            int start = pmatch[i * 2];
            if (start != -1)
            {
               int end = pmatch[i * 2 + 1];
               if (var == nullptr)
                  m_localVariables->create(varName, createValue(value->getValueAsCString() + start, end - start));
               else
                  var->setValue(createValue(value->getValueAsCString() + start, end - start));
               cgroups->append(createValue(value->getValueAsCString() + start, end - start));
            }
            else
            {
               if (var != nullptr)
                  var->setValue(createValue());
               cgroups->append(createValue());
            }
         }

         result = createValue(cgroups);
      }
      else
      {
         result = createValue(false);  // No match
      }
      _pcre_free_t(preg);
   }
   else
   {
      error(NXSL_ERR_REGEXP_ERROR);
      result = nullptr;
   }
   return result;
}

/**
 * Trace
 */
void NXSL_VM::trace(int level, const TCHAR *text)
{
	if (m_env != nullptr)
		m_env->trace(level, text);
}

/**
 * Report error
 */
void NXSL_VM::error(int errorCode, int sourceLine)
{
   m_errorCode = errorCode;
   m_errorLine = (sourceLine == -1) ?
            (((m_cp == INVALID_ADDRESS) || (m_cp >= static_cast<UINT32>(m_instructionSet.size()))) ?
                     0 : m_instructionSet.get(m_cp)->m_sourceLine) : sourceLine;

   TCHAR buffer[1024];
   if ((errorCode == NXSL_ERR_ASSERTION_FAILED) && (m_assertMessage != nullptr) && (*m_assertMessage != 0))
      _sntprintf(buffer, 1024, _T("Error %d in line %d: %s (%s)"), errorCode, m_errorLine, GetErrorMessage(errorCode), m_assertMessage);
   else
      _sntprintf(buffer, 1024, _T("Error %d in line %d: %s"), errorCode, m_errorLine, GetErrorMessage(errorCode));
   MemFree(m_errorText);
   m_errorText = MemCopyString(buffer);

   m_cp = INVALID_ADDRESS;
}

/**
 * Set persistent storage. Passing nullptr will switch VM to local storage.
 */
void NXSL_VM::setStorage(NXSL_Storage *storage)
{
   if (storage != nullptr)
   {
      m_storage = storage;
   }
   else
   {
      if (m_localStorage == nullptr)
         m_localStorage = new NXSL_LocalStorage(this);
      m_storage = m_localStorage;
   }
}

/**
 * Get array's attribute
 */
void NXSL_VM::getArrayAttribute(NXSL_Array *a, const char *attribute, bool safe)
{
   if (!strcmp(attribute, "maxIndex"))
   {
      m_dataStack.push((a->size() > 0) ? createValue((INT32)a->getMaxIndex()) : createValue());
   }
   else if (!strcmp(attribute, "minIndex"))
   {
      m_dataStack.push((a->size() > 0) ? createValue((INT32)a->getMinIndex()) : createValue());
   }
   else if (!strcmp(attribute, "size"))
   {
      m_dataStack.push(createValue((INT32)a->size()));
   }
   else
   {
      if (safe)
         m_dataStack.push(createValue());
      else
         error(NXSL_ERR_NO_SUCH_ATTRIBUTE);
   }
}

/**
 * Get hash map's attribute
 */
void NXSL_VM::getHashMapAttribute(NXSL_HashMap *m, const char *attribute, bool safe)
{
   if (!strcmp(attribute, "keys"))
   {
      m_dataStack.push(m->getKeys());
   }
   else if (!strcmp(attribute, "size"))
   {
      m_dataStack.push(createValue((INT32)m->size()));
   }
   else if (!strcmp(attribute, "values"))
   {
      m_dataStack.push(m->getValues());
   }
   else
   {
      if (safe)
         m_dataStack.push(createValue());
      else
         error(NXSL_ERR_NO_SUCH_ATTRIBUTE);
   }
}

/**
 * Push VM property
 */
void NXSL_VM::pushProperty(const NXSL_Identifier& name)
{
   if (!strcmp(name.value, "NXSL::Classes"))
   {
      NXSL_Array *a = new NXSL_Array(this);
      for(size_t i = 0; i < g_nxslClassRegistry.size; i++)
      {
         a->append(createValue(new NXSL_Object(this, &g_nxslMetaClass, g_nxslClassRegistry.classes[i])));
      }
      m_dataStack.push(createValue(a));
   }
   else if (!strcmp(name.value, "NXSL::Functions"))
   {
      StringSet *functions = m_env->getAllFunctions();
      for(int i = 0; i < m_functions.size(); i++)
      {
#ifdef UNICODE
         functions->addPreallocated(WideStringFromUTF8String(m_functions.get(i)->m_name.value));
#else
         functions->add(m_functions.get(i)->m_name.value);
#endif
      }
      m_dataStack.push(createValue(new NXSL_Array(this, *functions)));
      delete functions;
   }
   else
   {
      m_dataStack.push(createValue());
   }
}

/**
 * Set context object
 */
void NXSL_VM::setContextObject(NXSL_Value *value)
{
   destroyValue(m_context);
   if ((value != nullptr) && value->isObject())
   {
      m_context = value;
      if (m_contextVariables == nullptr)
         m_contextVariables = new NXSL_VariableSystem(this, NXSL_VariableSystemType::CONTEXT);
      else
         m_contextVariables->clear();
   }
   else
   {
      m_context = nullptr;
      destroyValue(value);
      delete_and_null(m_contextVariables);
   }
}

/**
 * Set security context
 */
void NXSL_VM::setSecurityContext(NXSL_SecurityContext *context)
{
   delete m_securityContext;
   m_securityContext = context;
}

/**
 * Dump VM code
 */
void NXSL_VM::dump(FILE *fp) const
{
   NXSL_ProgramBuilder::dump(fp, m_instructionSet);

   if (!m_functions.isEmpty())
   {
      _ftprintf(fp, _T("\nFunctions:\n"));
      for(int i = 0; i < m_functions.size(); i++)
      {
         NXSL_Function *f = m_functions.get(i);
         _ftprintf(fp, _T("  %04X %hs\n"), f->m_addr, f->m_name.value);
      }
   }
}
