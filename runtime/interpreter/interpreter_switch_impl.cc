/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "interpreter_common.h"

namespace art {
namespace interpreter {

#define HANDLE_PENDING_EXCEPTION()                                                              \
  do {                                                                                          \
    CHECK(self->IsExceptionPending());                                                          \
    uint32_t found_dex_pc = FindNextInstructionFollowingException(self, shadow_frame,           \
                                                                  inst->GetDexPc(insns),        \
                                                                  this_object_ref,              \
                                                                  instrumentation);             \
    if (found_dex_pc == DexFile::kDexNoIndex) {                                                 \
      return JValue(); /* Handled in caller. */                                                 \
    } else {                                                                                    \
      int32_t displacement = static_cast<int32_t>(found_dex_pc) - static_cast<int32_t>(dex_pc); \
      inst = inst->RelativeAt(displacement);                                                    \
    }                                                                                           \
  } while (false)

#define POSSIBLY_HANDLE_PENDING_EXCEPTION(_is_exception_pending, _next_function)  \
  do {                                                                            \
    if (UNLIKELY(_is_exception_pending)) {                                        \
      HANDLE_PENDING_EXCEPTION();                                                 \
    } else {                                                                      \
      inst = inst->_next_function();                                              \
    }                                                                             \
  } while (false)

// Code to run before each dex instruction.
#define PREAMBLE()

template<bool do_access_check>
static JValue ExecuteSwitchImpl(Thread* self, MethodHelper& mh, const DexFile::CodeItem* code_item,
                                ShadowFrame& shadow_frame, JValue result_register) {
  if (UNLIKELY(!shadow_frame.HasReferenceArray())) {
    LOG(FATAL) << "Invalid shadow frame for interpreter use";
    return JValue();
  }
  self->VerifyStack();
  instrumentation::Instrumentation* const instrumentation = Runtime::Current()->GetInstrumentation();

  // As the 'this' object won't change during the execution of current code, we
  // want to cache it in local variables. Nevertheless, in order to let the
  // garbage collector access it, we store it into sirt references.
  SirtRef<Object> this_object_ref(self, shadow_frame.GetThisObject(code_item->ins_size_));

  uint32_t dex_pc = shadow_frame.GetDexPC();
  if (LIKELY(dex_pc == 0)) {  // We are entering the method as opposed to deoptimizing..
    if (UNLIKELY(instrumentation->HasMethodEntryListeners())) {
      instrumentation->MethodEnterEvent(self, this_object_ref.get(),
                                        shadow_frame.GetMethod(), 0);
    }
  }
  const uint16_t* const insns = code_item->insns_;
  const Instruction* inst = Instruction::At(insns + dex_pc);
  while (true) {
    dex_pc = inst->GetDexPc(insns);
    shadow_frame.SetDexPC(dex_pc);
    if (UNLIKELY(self->TestAllFlags())) {
      CheckSuspend(self);
    }
    if (UNLIKELY(instrumentation->HasDexPcListeners())) {
      instrumentation->DexPcMovedEvent(self, this_object_ref.get(),
                                       shadow_frame.GetMethod(), dex_pc);
    }
    TraceExecution(shadow_frame, inst, dex_pc, mh);
    switch (inst->Opcode()) {
      case Instruction::NOP:
        PREAMBLE();
        inst = inst->Next_1xx();
        break;
      case Instruction::MOVE:
        PREAMBLE();
        shadow_frame.SetVReg(inst->VRegA_12x(),
                             shadow_frame.GetVReg(inst->VRegB_12x()));
        inst = inst->Next_1xx();
        break;
      case Instruction::MOVE_FROM16:
        PREAMBLE();
        shadow_frame.SetVReg(inst->VRegA_22x(),
                             shadow_frame.GetVReg(inst->VRegB_22x()));
        inst = inst->Next_2xx();
        break;
      case Instruction::MOVE_16:
        PREAMBLE();
        shadow_frame.SetVReg(inst->VRegA_32x(),
                             shadow_frame.GetVReg(inst->VRegB_32x()));
        inst = inst->Next_3xx();
        break;
      case Instruction::MOVE_WIDE:
        PREAMBLE();
        shadow_frame.SetVRegLong(inst->VRegA_12x(),
                                 shadow_frame.GetVRegLong(inst->VRegB_12x()));
        inst = inst->Next_1xx();
        break;
      case Instruction::MOVE_WIDE_FROM16:
        PREAMBLE();
        shadow_frame.SetVRegLong(inst->VRegA_22x(),
                                 shadow_frame.GetVRegLong(inst->VRegB_22x()));
        inst = inst->Next_2xx();
        break;
      case Instruction::MOVE_WIDE_16:
        PREAMBLE();
        shadow_frame.SetVRegLong(inst->VRegA_32x(),
                                 shadow_frame.GetVRegLong(inst->VRegB_32x()));
        inst = inst->Next_3xx();
        break;
      case Instruction::MOVE_OBJECT:
        PREAMBLE();
        shadow_frame.SetVRegReference(inst->VRegA_12x(),
                                      shadow_frame.GetVRegReference(inst->VRegB_12x()));
        inst = inst->Next_1xx();
        break;
      case Instruction::MOVE_OBJECT_FROM16:
        PREAMBLE();
        shadow_frame.SetVRegReference(inst->VRegA_22x(),
                                      shadow_frame.GetVRegReference(inst->VRegB_22x()));
        inst = inst->Next_2xx();
        break;
      case Instruction::MOVE_OBJECT_16:
        PREAMBLE();
        shadow_frame.SetVRegReference(inst->VRegA_32x(),
                                      shadow_frame.GetVRegReference(inst->VRegB_32x()));
        inst = inst->Next_3xx();
        break;
      case Instruction::MOVE_RESULT:
        PREAMBLE();
        shadow_frame.SetVReg(inst->VRegA_11x(), result_register.GetI());
        inst = inst->Next_1xx();
        break;
      case Instruction::MOVE_RESULT_WIDE:
        PREAMBLE();
        shadow_frame.SetVRegLong(inst->VRegA_11x(), result_register.GetJ());
        inst = inst->Next_1xx();
        break;
      case Instruction::MOVE_RESULT_OBJECT:
        PREAMBLE();
        shadow_frame.SetVRegReference(inst->VRegA_11x(), result_register.GetL());
        inst = inst->Next_1xx();
        break;
      case Instruction::MOVE_EXCEPTION: {
        PREAMBLE();
        Throwable* exception = self->GetException(NULL);
        self->ClearException();
        shadow_frame.SetVRegReference(inst->VRegA_11x(), exception);
        inst = inst->Next_1xx();
        break;
      }
      case Instruction::RETURN_VOID: {
        PREAMBLE();
        JValue result;
        if (UNLIKELY(instrumentation->HasMethodExitListeners())) {
          instrumentation->MethodExitEvent(self, this_object_ref.get(),
                                           shadow_frame.GetMethod(), inst->GetDexPc(insns),
                                           result);
        }
        return result;
      }
      case Instruction::RETURN_VOID_BARRIER: {
        PREAMBLE();
        ANDROID_MEMBAR_STORE();
        JValue result;
        if (UNLIKELY(instrumentation->HasMethodExitListeners())) {
          instrumentation->MethodExitEvent(self, this_object_ref.get(),
                                           shadow_frame.GetMethod(), inst->GetDexPc(insns),
                                           result);
        }
        return result;
      }
      case Instruction::RETURN: {
        PREAMBLE();
        JValue result;
        result.SetJ(0);
        result.SetI(shadow_frame.GetVReg(inst->VRegA_11x()));
        if (UNLIKELY(instrumentation->HasMethodExitListeners())) {
          instrumentation->MethodExitEvent(self, this_object_ref.get(),
                                           shadow_frame.GetMethod(), inst->GetDexPc(insns),
                                           result);
        }
        return result;
      }
      case Instruction::RETURN_WIDE: {
        PREAMBLE();
        JValue result;
        result.SetJ(shadow_frame.GetVRegLong(inst->VRegA_11x()));
        if (UNLIKELY(instrumentation->HasMethodExitListeners())) {
          instrumentation->MethodExitEvent(self, this_object_ref.get(),
                                           shadow_frame.GetMethod(), inst->GetDexPc(insns),
                                           result);
        }
        return result;
      }
      case Instruction::RETURN_OBJECT: {
        PREAMBLE();
        JValue result;
        result.SetJ(0);
        result.SetL(shadow_frame.GetVRegReference(inst->VRegA_11x()));
        if (UNLIKELY(instrumentation->HasMethodExitListeners())) {
          instrumentation->MethodExitEvent(self, this_object_ref.get(),
                                           shadow_frame.GetMethod(), inst->GetDexPc(insns),
                                           result);
        }
        return result;
      }
      case Instruction::CONST_4: {
        PREAMBLE();
        uint4_t dst = inst->VRegA_11n();
        int4_t val = inst->VRegB_11n();
        shadow_frame.SetVReg(dst, val);
        if (val == 0) {
          shadow_frame.SetVRegReference(dst, NULL);
        }
        inst = inst->Next_1xx();
        break;
      }
      case Instruction::CONST_16: {
        PREAMBLE();
        uint8_t dst = inst->VRegA_21s();
        int16_t val = inst->VRegB_21s();
        shadow_frame.SetVReg(dst, val);
        if (val == 0) {
          shadow_frame.SetVRegReference(dst, NULL);
        }
        inst = inst->Next_2xx();
        break;
      }
      case Instruction::CONST: {
        PREAMBLE();
        uint8_t dst = inst->VRegA_31i();
        int32_t val = inst->VRegB_31i();
        shadow_frame.SetVReg(dst, val);
        if (val == 0) {
          shadow_frame.SetVRegReference(dst, NULL);
        }
        inst = inst->Next_3xx();
        break;
      }
      case Instruction::CONST_HIGH16: {
        PREAMBLE();
        uint8_t dst = inst->VRegA_21h();
        int32_t val = static_cast<int32_t>(inst->VRegB_21h() << 16);
        shadow_frame.SetVReg(dst, val);
        if (val == 0) {
          shadow_frame.SetVRegReference(dst, NULL);
        }
        inst = inst->Next_2xx();
        break;
      }
      case Instruction::CONST_WIDE_16:
        PREAMBLE();
        shadow_frame.SetVRegLong(inst->VRegA_21s(), inst->VRegB_21s());
        inst = inst->Next_2xx();
        break;
      case Instruction::CONST_WIDE_32:
        PREAMBLE();
        shadow_frame.SetVRegLong(inst->VRegA_31i(), inst->VRegB_31i());
        inst = inst->Next_3xx();
        break;
      case Instruction::CONST_WIDE:
        PREAMBLE();
        shadow_frame.SetVRegLong(inst->VRegA_51l(), inst->VRegB_51l());
        inst = inst->Next_51l();
        break;
      case Instruction::CONST_WIDE_HIGH16:
        shadow_frame.SetVRegLong(inst->VRegA_21h(),
                                 static_cast<uint64_t>(inst->VRegB_21h()) << 48);
        inst = inst->Next_2xx();
        break;
      case Instruction::CONST_STRING: {
        PREAMBLE();
        String* s = ResolveString(self, mh,  inst->VRegB_21c());
        if (UNLIKELY(s == NULL)) {
          HANDLE_PENDING_EXCEPTION();
        } else {
          shadow_frame.SetVRegReference(inst->VRegA_21c(), s);
          inst = inst->Next_2xx();
        }
        break;
      }
      case Instruction::CONST_STRING_JUMBO: {
        PREAMBLE();
        String* s = ResolveString(self, mh,  inst->VRegB_31c());
        if (UNLIKELY(s == NULL)) {
          HANDLE_PENDING_EXCEPTION();
        } else {
          shadow_frame.SetVRegReference(inst->VRegA_31c(), s);
          inst = inst->Next_3xx();
        }
        break;
      }
      case Instruction::CONST_CLASS: {
        PREAMBLE();
        Class* c = ResolveVerifyAndClinit(inst->VRegB_21c(), shadow_frame.GetMethod(),
                                          self, false, do_access_check);
        if (UNLIKELY(c == NULL)) {
          HANDLE_PENDING_EXCEPTION();
        } else {
          shadow_frame.SetVRegReference(inst->VRegA_21c(), c);
          inst = inst->Next_2xx();
        }
        break;
      }
      case Instruction::MONITOR_ENTER: {
        PREAMBLE();
        Object* obj = shadow_frame.GetVRegReference(inst->VRegA_11x());
        if (UNLIKELY(obj == NULL)) {
          ThrowNullPointerExceptionFromDexPC(shadow_frame.GetCurrentLocationForThrow());
          HANDLE_PENDING_EXCEPTION();
        } else {
          DoMonitorEnter(self, obj);
          POSSIBLY_HANDLE_PENDING_EXCEPTION(self->IsExceptionPending(), Next_1xx);
        }
        break;
      }
      case Instruction::MONITOR_EXIT: {
        PREAMBLE();
        Object* obj = shadow_frame.GetVRegReference(inst->VRegA_11x());
        if (UNLIKELY(obj == NULL)) {
          ThrowNullPointerExceptionFromDexPC(shadow_frame.GetCurrentLocationForThrow());
          HANDLE_PENDING_EXCEPTION();
        } else {
          DoMonitorExit(self, obj);
          POSSIBLY_HANDLE_PENDING_EXCEPTION(self->IsExceptionPending(), Next_1xx);
        }
        break;
      }
      case Instruction::CHECK_CAST: {
        PREAMBLE();
        Class* c = ResolveVerifyAndClinit(inst->VRegB_21c(), shadow_frame.GetMethod(),
                                          self, false, do_access_check);
        if (UNLIKELY(c == NULL)) {
          HANDLE_PENDING_EXCEPTION();
        } else {
          Object* obj = shadow_frame.GetVRegReference(inst->VRegA_21c());
          if (UNLIKELY(obj != NULL && !obj->InstanceOf(c))) {
            ThrowClassCastException(c, obj->GetClass());
            HANDLE_PENDING_EXCEPTION();
          } else {
            inst = inst->Next_2xx();
          }
        }
        break;
      }
      case Instruction::INSTANCE_OF: {
        PREAMBLE();
        Class* c = ResolveVerifyAndClinit(inst->VRegC_22c(), shadow_frame.GetMethod(),
                                          self, false, do_access_check);
        if (UNLIKELY(c == NULL)) {
          HANDLE_PENDING_EXCEPTION();
        } else {
          Object* obj = shadow_frame.GetVRegReference(inst->VRegB_22c());
          shadow_frame.SetVReg(inst->VRegA_22c(), (obj != NULL && obj->InstanceOf(c)) ? 1 : 0);
          inst = inst->Next_2xx();
        }
        break;
      }
      case Instruction::ARRAY_LENGTH:  {
        PREAMBLE();
        Object* array = shadow_frame.GetVRegReference(inst->VRegB_12x());
        if (UNLIKELY(array == NULL)) {
          ThrowNullPointerExceptionFromDexPC(shadow_frame.GetCurrentLocationForThrow());
          HANDLE_PENDING_EXCEPTION();
        } else {
          shadow_frame.SetVReg(inst->VRegA_12x(), array->AsArray()->GetLength());
          inst = inst->Next_1xx();
        }
        break;
      }
      case Instruction::NEW_INSTANCE: {
        PREAMBLE();
        Object* obj = AllocObjectFromCode(inst->VRegB_21c(), shadow_frame.GetMethod(),
                                          self, do_access_check);
        if (UNLIKELY(obj == NULL)) {
          HANDLE_PENDING_EXCEPTION();
        } else {
          shadow_frame.SetVRegReference(inst->VRegA_21c(), obj);
          inst = inst->Next_2xx();
        }
        break;
      }
      case Instruction::NEW_ARRAY: {
        PREAMBLE();
        int32_t length = shadow_frame.GetVReg(inst->VRegB_22c());
        Object* obj = AllocArrayFromCode(inst->VRegC_22c(), shadow_frame.GetMethod(),
                                         length, self, do_access_check);
        if (UNLIKELY(obj == NULL)) {
          HANDLE_PENDING_EXCEPTION();
        } else {
          shadow_frame.SetVRegReference(inst->VRegA_22c(), obj);
          inst = inst->Next_2xx();
        }
        break;
      }
      case Instruction::FILLED_NEW_ARRAY: {
        PREAMBLE();
        bool success = DoFilledNewArray<false, do_access_check>(inst, shadow_frame,
                                                                self, &result_register);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_3xx);
        break;
      }
      case Instruction::FILLED_NEW_ARRAY_RANGE: {
        PREAMBLE();
        bool success = DoFilledNewArray<true, do_access_check>(inst, shadow_frame,
                                                               self, &result_register);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_3xx);
        break;
      }
      case Instruction::FILL_ARRAY_DATA: {
        PREAMBLE();
        Object* obj = shadow_frame.GetVRegReference(inst->VRegA_31t());
        if (UNLIKELY(obj == NULL)) {
          ThrowNullPointerException(NULL, "null array in FILL_ARRAY_DATA");
          HANDLE_PENDING_EXCEPTION();
          break;
        }
        Array* array = obj->AsArray();
        DCHECK(array->IsArrayInstance() && !array->IsObjectArray());
        const uint16_t* payload_addr = reinterpret_cast<const uint16_t*>(inst) + inst->VRegB_31t();
        const Instruction::ArrayDataPayload* payload =
            reinterpret_cast<const Instruction::ArrayDataPayload*>(payload_addr);
        if (UNLIKELY(static_cast<int32_t>(payload->element_count) > array->GetLength())) {
          self->ThrowNewExceptionF(shadow_frame.GetCurrentLocationForThrow(),
                                   "Ljava/lang/ArrayIndexOutOfBoundsException;",
                                   "failed FILL_ARRAY_DATA; length=%d, index=%d",
                                   array->GetLength(), payload->element_count);
          HANDLE_PENDING_EXCEPTION();
          break;
        }
        uint32_t size_in_bytes = payload->element_count * payload->element_width;
        memcpy(array->GetRawData(payload->element_width), payload->data, size_in_bytes);
        inst = inst->Next_3xx();
        break;
      }
      case Instruction::THROW: {
        PREAMBLE();
        Object* exception = shadow_frame.GetVRegReference(inst->VRegA_11x());
        if (UNLIKELY(exception == NULL)) {
          ThrowNullPointerException(NULL, "throw with null exception");
        } else {
          self->SetException(shadow_frame.GetCurrentLocationForThrow(), exception->AsThrowable());
        }
        HANDLE_PENDING_EXCEPTION();
        break;
      }
      case Instruction::GOTO: {
        PREAMBLE();
        inst = inst->RelativeAt(inst->VRegA_10t());
        break;
      }
      case Instruction::GOTO_16: {
        PREAMBLE();
        inst = inst->RelativeAt(inst->VRegA_20t());
        break;
      }
      case Instruction::GOTO_32: {
        PREAMBLE();
        inst = inst->RelativeAt(inst->VRegA_30t());
        break;
      }
      case Instruction::PACKED_SWITCH: {
        PREAMBLE();
        int32_t offset = DoPackedSwitch(inst, shadow_frame);
        inst = inst->RelativeAt(offset);
        break;
      }
      case Instruction::SPARSE_SWITCH: {
        PREAMBLE();
        int32_t offset = DoSparseSwitch(inst, shadow_frame);
        inst = inst->RelativeAt(offset);
        break;
      }
      case Instruction::CMPL_FLOAT: {
        PREAMBLE();
        float val1 = shadow_frame.GetVRegFloat(inst->VRegB_23x());
        float val2 = shadow_frame.GetVRegFloat(inst->VRegC_23x());
        int32_t result;
        if (val1 > val2) {
          result = 1;
        } else if (val1 == val2) {
          result = 0;
        } else {
          result = -1;
        }
        shadow_frame.SetVReg(inst->VRegA_23x(), result);
        inst = inst->Next_2xx();
        break;
      }
      case Instruction::CMPG_FLOAT: {
        PREAMBLE();
        float val1 = shadow_frame.GetVRegFloat(inst->VRegB_23x());
        float val2 = shadow_frame.GetVRegFloat(inst->VRegC_23x());
        int32_t result;
        if (val1 < val2) {
          result = -1;
        } else if (val1 == val2) {
          result = 0;
        } else {
          result = 1;
        }
        shadow_frame.SetVReg(inst->VRegA_23x(), result);
        inst = inst->Next_2xx();
        break;
      }
      case Instruction::CMPL_DOUBLE: {
        PREAMBLE();
        double val1 = shadow_frame.GetVRegDouble(inst->VRegB_23x());
        double val2 = shadow_frame.GetVRegDouble(inst->VRegC_23x());
        int32_t result;
        if (val1 > val2) {
          result = 1;
        } else if (val1 == val2) {
          result = 0;
        } else {
          result = -1;
        }
        shadow_frame.SetVReg(inst->VRegA_23x(), result);
        inst = inst->Next_2xx();
        break;
      }

      case Instruction::CMPG_DOUBLE: {
        PREAMBLE();
        double val1 = shadow_frame.GetVRegDouble(inst->VRegB_23x());
        double val2 = shadow_frame.GetVRegDouble(inst->VRegC_23x());
        int32_t result;
        if (val1 < val2) {
          result = -1;
        } else if (val1 == val2) {
          result = 0;
        } else {
          result = 1;
        }
        shadow_frame.SetVReg(inst->VRegA_23x(), result);
        inst = inst->Next_2xx();
        break;
      }
      case Instruction::CMP_LONG: {
        PREAMBLE();
        int64_t val1 = shadow_frame.GetVRegLong(inst->VRegB_23x());
        int64_t val2 = shadow_frame.GetVRegLong(inst->VRegC_23x());
        int32_t result;
        if (val1 > val2) {
          result = 1;
        } else if (val1 == val2) {
          result = 0;
        } else {
          result = -1;
        }
        shadow_frame.SetVReg(inst->VRegA_23x(), result);
        inst = inst->Next_2xx();
        break;
      }
      case Instruction::IF_EQ: {
        PREAMBLE();
        if (shadow_frame.GetVReg(inst->VRegA_22t()) == shadow_frame.GetVReg(inst->VRegB_22t())) {
          inst = inst->RelativeAt(inst->VRegC_22t());
        } else {
          inst = inst->Next_2xx();
        }
        break;
      }
      case Instruction::IF_NE: {
        PREAMBLE();
        if (shadow_frame.GetVReg(inst->VRegA_22t()) != shadow_frame.GetVReg(inst->VRegB_22t())) {
          inst = inst->RelativeAt(inst->VRegC_22t());
        } else {
          inst = inst->Next_2xx();
        }
        break;
      }
      case Instruction::IF_LT: {
        PREAMBLE();
        if (shadow_frame.GetVReg(inst->VRegA_22t()) < shadow_frame.GetVReg(inst->VRegB_22t())) {
          inst = inst->RelativeAt(inst->VRegC_22t());
        } else {
          inst = inst->Next_2xx();
        }
        break;
      }
      case Instruction::IF_GE: {
        PREAMBLE();
        if (shadow_frame.GetVReg(inst->VRegA_22t()) >= shadow_frame.GetVReg(inst->VRegB_22t())) {
          inst = inst->RelativeAt(inst->VRegC_22t());
        } else {
          inst = inst->Next_2xx();
        }
        break;
      }
      case Instruction::IF_GT: {
        PREAMBLE();
        if (shadow_frame.GetVReg(inst->VRegA_22t()) > shadow_frame.GetVReg(inst->VRegB_22t())) {
          inst = inst->RelativeAt(inst->VRegC_22t());
        } else {
          inst = inst->Next_2xx();
        }
        break;
      }
      case Instruction::IF_LE: {
        PREAMBLE();
        if (shadow_frame.GetVReg(inst->VRegA_22t()) <= shadow_frame.GetVReg(inst->VRegB_22t())) {
          inst = inst->RelativeAt(inst->VRegC_22t());
        } else {
          inst = inst->Next_2xx();
        }
        break;
      }
      case Instruction::IF_EQZ: {
        PREAMBLE();
        if (shadow_frame.GetVReg(inst->VRegA_21t()) == 0) {
          inst = inst->RelativeAt(inst->VRegB_21t());
        } else {
          inst = inst->Next_2xx();
        }
        break;
      }
      case Instruction::IF_NEZ: {
        PREAMBLE();
        if (shadow_frame.GetVReg(inst->VRegA_21t()) != 0) {
          inst = inst->RelativeAt(inst->VRegB_21t());
        } else {
          inst = inst->Next_2xx();
        }
        break;
      }
      case Instruction::IF_LTZ: {
        PREAMBLE();
        if (shadow_frame.GetVReg(inst->VRegA_21t()) < 0) {
          inst = inst->RelativeAt(inst->VRegB_21t());
        } else {
          inst = inst->Next_2xx();
        }
        break;
      }
      case Instruction::IF_GEZ: {
        PREAMBLE();
        if (shadow_frame.GetVReg(inst->VRegA_21t()) >= 0) {
          inst = inst->RelativeAt(inst->VRegB_21t());
        } else {
          inst = inst->Next_2xx();
        }
        break;
      }
      case Instruction::IF_GTZ: {
        PREAMBLE();
        if (shadow_frame.GetVReg(inst->VRegA_21t()) > 0) {
          inst = inst->RelativeAt(inst->VRegB_21t());
        } else {
          inst = inst->Next_2xx();
        }
        break;
      }
      case Instruction::IF_LEZ:  {
        PREAMBLE();
        if (shadow_frame.GetVReg(inst->VRegA_21t()) <= 0) {
          inst = inst->RelativeAt(inst->VRegB_21t());
        } else {
          inst = inst->Next_2xx();
        }
        break;
      }
      case Instruction::AGET_BOOLEAN: {
        PREAMBLE();
        Object* a = shadow_frame.GetVRegReference(inst->VRegB_23x());
        if (UNLIKELY(a == NULL)) {
          ThrowNullPointerExceptionFromDexPC(shadow_frame.GetCurrentLocationForThrow());
          HANDLE_PENDING_EXCEPTION();
          break;
        }
        int32_t index = shadow_frame.GetVReg(inst->VRegC_23x());
        BooleanArray* array = a->AsBooleanArray();
        if (LIKELY(array->IsValidIndex(index))) {
          shadow_frame.SetVReg(inst->VRegA_23x(), array->GetData()[index]);
          inst = inst->Next_2xx();
        } else {
          HANDLE_PENDING_EXCEPTION();
        }
        break;
      }
      case Instruction::AGET_BYTE: {
        PREAMBLE();
        Object* a = shadow_frame.GetVRegReference(inst->VRegB_23x());
        if (UNLIKELY(a == NULL)) {
          ThrowNullPointerExceptionFromDexPC(shadow_frame.GetCurrentLocationForThrow());
          HANDLE_PENDING_EXCEPTION();
          break;
        }
        int32_t index = shadow_frame.GetVReg(inst->VRegC_23x());
        ByteArray* array = a->AsByteArray();
        if (LIKELY(array->IsValidIndex(index))) {
          shadow_frame.SetVReg(inst->VRegA_23x(), array->GetData()[index]);
          inst = inst->Next_2xx();
        } else {
          HANDLE_PENDING_EXCEPTION();
        }
        break;
      }
      case Instruction::AGET_CHAR: {
        PREAMBLE();
        Object* a = shadow_frame.GetVRegReference(inst->VRegB_23x());
        if (UNLIKELY(a == NULL)) {
          ThrowNullPointerExceptionFromDexPC(shadow_frame.GetCurrentLocationForThrow());
          HANDLE_PENDING_EXCEPTION();
          break;
        }
        int32_t index = shadow_frame.GetVReg(inst->VRegC_23x());
        CharArray* array = a->AsCharArray();
        if (LIKELY(array->IsValidIndex(index))) {
          shadow_frame.SetVReg(inst->VRegA_23x(), array->GetData()[index]);
          inst = inst->Next_2xx();
        } else {
          HANDLE_PENDING_EXCEPTION();
        }
        break;
      }
      case Instruction::AGET_SHORT: {
        PREAMBLE();
        Object* a = shadow_frame.GetVRegReference(inst->VRegB_23x());
        if (UNLIKELY(a == NULL)) {
          ThrowNullPointerExceptionFromDexPC(shadow_frame.GetCurrentLocationForThrow());
          HANDLE_PENDING_EXCEPTION();
          break;
        }
        int32_t index = shadow_frame.GetVReg(inst->VRegC_23x());
        ShortArray* array = a->AsShortArray();
        if (LIKELY(array->IsValidIndex(index))) {
          shadow_frame.SetVReg(inst->VRegA_23x(), array->GetData()[index]);
          inst = inst->Next_2xx();
        } else {
          HANDLE_PENDING_EXCEPTION();
        }
        break;
      }
      case Instruction::AGET: {
        PREAMBLE();
        Object* a = shadow_frame.GetVRegReference(inst->VRegB_23x());
        if (UNLIKELY(a == NULL)) {
          ThrowNullPointerExceptionFromDexPC(shadow_frame.GetCurrentLocationForThrow());
          HANDLE_PENDING_EXCEPTION();
          break;
        }
        int32_t index = shadow_frame.GetVReg(inst->VRegC_23x());
        IntArray* array = a->AsIntArray();
        if (LIKELY(array->IsValidIndex(index))) {
          shadow_frame.SetVReg(inst->VRegA_23x(), array->GetData()[index]);
          inst = inst->Next_2xx();
        } else {
          HANDLE_PENDING_EXCEPTION();
        }
        break;
      }
      case Instruction::AGET_WIDE:  {
        PREAMBLE();
        Object* a = shadow_frame.GetVRegReference(inst->VRegB_23x());
        if (UNLIKELY(a == NULL)) {
          ThrowNullPointerExceptionFromDexPC(shadow_frame.GetCurrentLocationForThrow());
          HANDLE_PENDING_EXCEPTION();
          break;
        }
        int32_t index = shadow_frame.GetVReg(inst->VRegC_23x());
        LongArray* array = a->AsLongArray();
        if (LIKELY(array->IsValidIndex(index))) {
          shadow_frame.SetVRegLong(inst->VRegA_23x(), array->GetData()[index]);
          inst = inst->Next_2xx();
        } else {
          HANDLE_PENDING_EXCEPTION();
        }
        break;
      }
      case Instruction::AGET_OBJECT: {
        PREAMBLE();
        Object* a = shadow_frame.GetVRegReference(inst->VRegB_23x());
        if (UNLIKELY(a == NULL)) {
          ThrowNullPointerExceptionFromDexPC(shadow_frame.GetCurrentLocationForThrow());
          HANDLE_PENDING_EXCEPTION();
          break;
        }
        int32_t index = shadow_frame.GetVReg(inst->VRegC_23x());
        ObjectArray<Object>* array = a->AsObjectArray<Object>();
        if (LIKELY(array->IsValidIndex(index))) {
          shadow_frame.SetVRegReference(inst->VRegA_23x(), array->GetWithoutChecks(index));
          inst = inst->Next_2xx();
        } else {
          HANDLE_PENDING_EXCEPTION();
        }
        break;
      }
      case Instruction::APUT_BOOLEAN: {
        PREAMBLE();
        Object* a = shadow_frame.GetVRegReference(inst->VRegB_23x());
        if (UNLIKELY(a == NULL)) {
          ThrowNullPointerExceptionFromDexPC(shadow_frame.GetCurrentLocationForThrow());
          HANDLE_PENDING_EXCEPTION();
          break;
        }
        uint8_t val = shadow_frame.GetVReg(inst->VRegA_23x());
        int32_t index = shadow_frame.GetVReg(inst->VRegC_23x());
        BooleanArray* array = a->AsBooleanArray();
        if (LIKELY(array->IsValidIndex(index))) {
          array->GetData()[index] = val;
          inst = inst->Next_2xx();
        } else {
          HANDLE_PENDING_EXCEPTION();
        }
        break;
      }
      case Instruction::APUT_BYTE: {
        PREAMBLE();
        Object* a = shadow_frame.GetVRegReference(inst->VRegB_23x());
        if (UNLIKELY(a == NULL)) {
          ThrowNullPointerExceptionFromDexPC(shadow_frame.GetCurrentLocationForThrow());
          HANDLE_PENDING_EXCEPTION();
          break;
        }
        int8_t val = shadow_frame.GetVReg(inst->VRegA_23x());
        int32_t index = shadow_frame.GetVReg(inst->VRegC_23x());
        ByteArray* array = a->AsByteArray();
        if (LIKELY(array->IsValidIndex(index))) {
          array->GetData()[index] = val;
          inst = inst->Next_2xx();
        } else {
          HANDLE_PENDING_EXCEPTION();
        }
        break;
      }
      case Instruction::APUT_CHAR: {
        PREAMBLE();
        Object* a = shadow_frame.GetVRegReference(inst->VRegB_23x());
        if (UNLIKELY(a == NULL)) {
          ThrowNullPointerExceptionFromDexPC(shadow_frame.GetCurrentLocationForThrow());
          HANDLE_PENDING_EXCEPTION();
          break;
        }
        uint16_t val = shadow_frame.GetVReg(inst->VRegA_23x());
        int32_t index = shadow_frame.GetVReg(inst->VRegC_23x());
        CharArray* array = a->AsCharArray();
        if (LIKELY(array->IsValidIndex(index))) {
          array->GetData()[index] = val;
          inst = inst->Next_2xx();
        } else {
          HANDLE_PENDING_EXCEPTION();
        }
        break;
      }
      case Instruction::APUT_SHORT: {
        PREAMBLE();
        Object* a = shadow_frame.GetVRegReference(inst->VRegB_23x());
        if (UNLIKELY(a == NULL)) {
          ThrowNullPointerExceptionFromDexPC(shadow_frame.GetCurrentLocationForThrow());
          HANDLE_PENDING_EXCEPTION();
          break;
        }
        int16_t val = shadow_frame.GetVReg(inst->VRegA_23x());
        int32_t index = shadow_frame.GetVReg(inst->VRegC_23x());
        ShortArray* array = a->AsShortArray();
        if (LIKELY(array->IsValidIndex(index))) {
          array->GetData()[index] = val;
          inst = inst->Next_2xx();
        } else {
          HANDLE_PENDING_EXCEPTION();
        }
        break;
      }
      case Instruction::APUT: {
        PREAMBLE();
        Object* a = shadow_frame.GetVRegReference(inst->VRegB_23x());
        if (UNLIKELY(a == NULL)) {
          ThrowNullPointerExceptionFromDexPC(shadow_frame.GetCurrentLocationForThrow());
          HANDLE_PENDING_EXCEPTION();
          break;
        }
        int32_t val = shadow_frame.GetVReg(inst->VRegA_23x());
        int32_t index = shadow_frame.GetVReg(inst->VRegC_23x());
        IntArray* array = a->AsIntArray();
        if (LIKELY(array->IsValidIndex(index))) {
          array->GetData()[index] = val;
          inst = inst->Next_2xx();
        } else {
          HANDLE_PENDING_EXCEPTION();
        }
        break;
      }
      case Instruction::APUT_WIDE: {
        PREAMBLE();
        Object* a = shadow_frame.GetVRegReference(inst->VRegB_23x());
        if (UNLIKELY(a == NULL)) {
          ThrowNullPointerExceptionFromDexPC(shadow_frame.GetCurrentLocationForThrow());
          HANDLE_PENDING_EXCEPTION();
          break;
        }
        int64_t val = shadow_frame.GetVRegLong(inst->VRegA_23x());
        int32_t index = shadow_frame.GetVReg(inst->VRegC_23x());
        LongArray* array = a->AsLongArray();
        if (LIKELY(array->IsValidIndex(index))) {
          array->GetData()[index] = val;
          inst = inst->Next_2xx();
        } else {
          HANDLE_PENDING_EXCEPTION();
        }
        break;
      }
      case Instruction::APUT_OBJECT: {
        PREAMBLE();
        Object* a = shadow_frame.GetVRegReference(inst->VRegB_23x());
        if (UNLIKELY(a == NULL)) {
          ThrowNullPointerExceptionFromDexPC(shadow_frame.GetCurrentLocationForThrow());
          HANDLE_PENDING_EXCEPTION();
          break;
        }
        int32_t index = shadow_frame.GetVReg(inst->VRegC_23x());
        Object* val = shadow_frame.GetVRegReference(inst->VRegA_23x());
        ObjectArray<Object>* array = a->AsObjectArray<Object>();
        if (LIKELY(array->IsValidIndex(index) && array->CheckAssignable(val))) {
          array->SetWithoutChecks(index, val);
          inst = inst->Next_2xx();
        } else {
          HANDLE_PENDING_EXCEPTION();
        }
        break;
      }
      case Instruction::IGET_BOOLEAN: {
        PREAMBLE();
        bool success = DoFieldGet<InstancePrimitiveRead, Primitive::kPrimBoolean, do_access_check>(self, shadow_frame, inst);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::IGET_BYTE: {
        PREAMBLE();
        bool success = DoFieldGet<InstancePrimitiveRead, Primitive::kPrimByte, do_access_check>(self, shadow_frame, inst);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::IGET_CHAR: {
        PREAMBLE();
        bool success = DoFieldGet<InstancePrimitiveRead, Primitive::kPrimChar, do_access_check>(self, shadow_frame, inst);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::IGET_SHORT: {
        PREAMBLE();
        bool success = DoFieldGet<InstancePrimitiveRead, Primitive::kPrimShort, do_access_check>(self, shadow_frame, inst);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::IGET: {
        PREAMBLE();
        bool success = DoFieldGet<InstancePrimitiveRead, Primitive::kPrimInt, do_access_check>(self, shadow_frame, inst);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::IGET_WIDE: {
        PREAMBLE();
        bool success = DoFieldGet<InstancePrimitiveRead, Primitive::kPrimLong, do_access_check>(self, shadow_frame, inst);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::IGET_OBJECT: {
        PREAMBLE();
        bool success = DoFieldGet<InstanceObjectRead, Primitive::kPrimNot, do_access_check>(self, shadow_frame, inst);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::IGET_QUICK: {
        PREAMBLE();
        bool success = DoIGetQuick<Primitive::kPrimInt>(self, shadow_frame, inst);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::IGET_WIDE_QUICK: {
        PREAMBLE();
        bool success = DoIGetQuick<Primitive::kPrimLong>(self, shadow_frame, inst);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::IGET_OBJECT_QUICK: {
        PREAMBLE();
        bool success = DoIGetQuick<Primitive::kPrimNot>(self, shadow_frame, inst);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::SGET_BOOLEAN: {
        PREAMBLE();
        bool success = DoFieldGet<StaticPrimitiveRead, Primitive::kPrimBoolean, do_access_check>(self, shadow_frame, inst);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::SGET_BYTE: {
        PREAMBLE();
        bool success = DoFieldGet<StaticPrimitiveRead, Primitive::kPrimByte, do_access_check>(self, shadow_frame, inst);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::SGET_CHAR: {
        PREAMBLE();
        bool success = DoFieldGet<StaticPrimitiveRead, Primitive::kPrimChar, do_access_check>(self, shadow_frame, inst);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::SGET_SHORT: {
        PREAMBLE();
        bool success = DoFieldGet<StaticPrimitiveRead, Primitive::kPrimShort, do_access_check>(self, shadow_frame, inst);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::SGET: {
        PREAMBLE();
        bool success = DoFieldGet<StaticPrimitiveRead, Primitive::kPrimInt, do_access_check>(self, shadow_frame, inst);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::SGET_WIDE: {
        PREAMBLE();
        bool success = DoFieldGet<StaticPrimitiveRead, Primitive::kPrimLong, do_access_check>(self, shadow_frame, inst);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::SGET_OBJECT: {
        PREAMBLE();
        bool success = DoFieldGet<StaticObjectRead, Primitive::kPrimNot, do_access_check>(self, shadow_frame, inst);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::IPUT_BOOLEAN: {
        PREAMBLE();
        bool success = DoFieldPut<InstancePrimitiveWrite, Primitive::kPrimBoolean, do_access_check>(self, shadow_frame, inst);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::IPUT_BYTE: {
        PREAMBLE();
        bool success = DoFieldPut<InstancePrimitiveWrite, Primitive::kPrimByte, do_access_check>(self, shadow_frame, inst);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::IPUT_CHAR: {
        PREAMBLE();
        bool success = DoFieldPut<InstancePrimitiveWrite, Primitive::kPrimChar, do_access_check>(self, shadow_frame, inst);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::IPUT_SHORT: {
        PREAMBLE();
        bool success = DoFieldPut<InstancePrimitiveWrite, Primitive::kPrimShort, do_access_check>(self, shadow_frame, inst);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::IPUT: {
        PREAMBLE();
        bool success = DoFieldPut<InstancePrimitiveWrite, Primitive::kPrimInt, do_access_check>(self, shadow_frame, inst);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::IPUT_WIDE: {
        PREAMBLE();
        bool success = DoFieldPut<InstancePrimitiveWrite, Primitive::kPrimLong, do_access_check>(self, shadow_frame, inst);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::IPUT_OBJECT: {
        PREAMBLE();
        bool success = DoFieldPut<InstanceObjectWrite, Primitive::kPrimNot, do_access_check>(self, shadow_frame, inst);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::IPUT_QUICK: {
        PREAMBLE();
        bool success = DoIPutQuick<Primitive::kPrimInt>(self, shadow_frame, inst);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::IPUT_WIDE_QUICK: {
        PREAMBLE();
        bool success = DoIPutQuick<Primitive::kPrimLong>(self, shadow_frame, inst);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::IPUT_OBJECT_QUICK: {
        PREAMBLE();
        bool success = DoIPutQuick<Primitive::kPrimNot>(self, shadow_frame, inst);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::SPUT_BOOLEAN: {
        PREAMBLE();
        bool success = DoFieldPut<StaticPrimitiveWrite, Primitive::kPrimBoolean, do_access_check>(self, shadow_frame, inst);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::SPUT_BYTE: {
        PREAMBLE();
        bool success = DoFieldPut<StaticPrimitiveWrite, Primitive::kPrimByte, do_access_check>(self, shadow_frame, inst);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::SPUT_CHAR: {
        PREAMBLE();
        bool success = DoFieldPut<StaticPrimitiveWrite, Primitive::kPrimChar, do_access_check>(self, shadow_frame, inst);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::SPUT_SHORT: {
        PREAMBLE();
        bool success = DoFieldPut<StaticPrimitiveWrite, Primitive::kPrimShort, do_access_check>(self, shadow_frame, inst);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::SPUT: {
        PREAMBLE();
        bool success = DoFieldPut<StaticPrimitiveWrite, Primitive::kPrimInt, do_access_check>(self, shadow_frame, inst);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::SPUT_WIDE: {
        PREAMBLE();
        bool success = DoFieldPut<StaticPrimitiveWrite, Primitive::kPrimLong, do_access_check>(self, shadow_frame, inst);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::SPUT_OBJECT: {
        PREAMBLE();
        bool success = DoFieldPut<StaticObjectWrite, Primitive::kPrimNot, do_access_check>(self, shadow_frame, inst);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::INVOKE_VIRTUAL: {
        PREAMBLE();
        bool success = DoInvoke<kVirtual, false, do_access_check>(self, shadow_frame, inst, &result_register);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_3xx);
        break;
      }
      case Instruction::INVOKE_VIRTUAL_RANGE: {
        PREAMBLE();
        bool success = DoInvoke<kVirtual, true, do_access_check>(self, shadow_frame, inst, &result_register);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_3xx);
        break;
      }
      case Instruction::INVOKE_SUPER: {
        PREAMBLE();
        bool success = DoInvoke<kSuper, false, do_access_check>(self, shadow_frame, inst, &result_register);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_3xx);
        break;
      }
      case Instruction::INVOKE_SUPER_RANGE: {
        PREAMBLE();
        bool success = DoInvoke<kSuper, true, do_access_check>(self, shadow_frame, inst, &result_register);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_3xx);
        break;
      }
      case Instruction::INVOKE_DIRECT: {
        PREAMBLE();
        bool success = DoInvoke<kDirect, false, do_access_check>(self, shadow_frame, inst, &result_register);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_3xx);
        break;
      }
      case Instruction::INVOKE_DIRECT_RANGE: {
        PREAMBLE();
        bool success = DoInvoke<kDirect, true, do_access_check>(self, shadow_frame, inst, &result_register);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_3xx);
        break;
      }
      case Instruction::INVOKE_INTERFACE: {
        PREAMBLE();
        bool success = DoInvoke<kInterface, false, do_access_check>(self, shadow_frame, inst, &result_register);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_3xx);
        break;
      }
      case Instruction::INVOKE_INTERFACE_RANGE: {
        PREAMBLE();
        bool success = DoInvoke<kInterface, true, do_access_check>(self, shadow_frame, inst, &result_register);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_3xx);
        break;
      }
      case Instruction::INVOKE_STATIC: {
        PREAMBLE();
        bool success = DoInvoke<kStatic, false, do_access_check>(self, shadow_frame, inst, &result_register);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_3xx);
        break;
      }
      case Instruction::INVOKE_STATIC_RANGE: {
        PREAMBLE();
        bool success = DoInvoke<kStatic, true, do_access_check>(self, shadow_frame, inst, &result_register);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_3xx);
        break;
      }
      case Instruction::INVOKE_VIRTUAL_QUICK: {
        PREAMBLE();
        bool success = DoInvokeVirtualQuick<false>(self, shadow_frame, inst, &result_register);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_3xx);
        break;
      }
      case Instruction::INVOKE_VIRTUAL_RANGE_QUICK: {
        PREAMBLE();
        bool success = DoInvokeVirtualQuick<true>(self, shadow_frame, inst, &result_register);
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_3xx);
        break;
      }
      case Instruction::NEG_INT:
        PREAMBLE();
        shadow_frame.SetVReg(inst->VRegA_12x(), -shadow_frame.GetVReg(inst->VRegB_12x()));
        inst = inst->Next_1xx();
        break;
      case Instruction::NOT_INT:
        PREAMBLE();
        shadow_frame.SetVReg(inst->VRegA_12x(), ~shadow_frame.GetVReg(inst->VRegB_12x()));
        inst = inst->Next_1xx();
        break;
      case Instruction::NEG_LONG:
        PREAMBLE();
        shadow_frame.SetVRegLong(inst->VRegA_12x(), -shadow_frame.GetVRegLong(inst->VRegB_12x()));
        inst = inst->Next_1xx();
        break;
      case Instruction::NOT_LONG:
        PREAMBLE();
        shadow_frame.SetVRegLong(inst->VRegA_12x(), ~shadow_frame.GetVRegLong(inst->VRegB_12x()));
        inst = inst->Next_1xx();
        break;
      case Instruction::NEG_FLOAT:
        PREAMBLE();
        shadow_frame.SetVRegFloat(inst->VRegA_12x(), -shadow_frame.GetVRegFloat(inst->VRegB_12x()));
        inst = inst->Next_1xx();
        break;
      case Instruction::NEG_DOUBLE:
        PREAMBLE();
        shadow_frame.SetVRegDouble(inst->VRegA_12x(), -shadow_frame.GetVRegDouble(inst->VRegB_12x()));
        inst = inst->Next_1xx();
        break;
      case Instruction::INT_TO_LONG:
        PREAMBLE();
        shadow_frame.SetVRegLong(inst->VRegA_12x(), shadow_frame.GetVReg(inst->VRegB_12x()));
        inst = inst->Next_1xx();
        break;
      case Instruction::INT_TO_FLOAT:
        PREAMBLE();
        shadow_frame.SetVRegFloat(inst->VRegA_12x(), shadow_frame.GetVReg(inst->VRegB_12x()));
        inst = inst->Next_1xx();
        break;
      case Instruction::INT_TO_DOUBLE:
        PREAMBLE();
        shadow_frame.SetVRegDouble(inst->VRegA_12x(), shadow_frame.GetVReg(inst->VRegB_12x()));
        inst = inst->Next_1xx();
        break;
      case Instruction::LONG_TO_INT:
        PREAMBLE();
        shadow_frame.SetVReg(inst->VRegA_12x(), shadow_frame.GetVRegLong(inst->VRegB_12x()));
        inst = inst->Next_1xx();
        break;
      case Instruction::LONG_TO_FLOAT:
        PREAMBLE();
        shadow_frame.SetVRegFloat(inst->VRegA_12x(), shadow_frame.GetVRegLong(inst->VRegB_12x()));
        inst = inst->Next_1xx();
        break;
      case Instruction::LONG_TO_DOUBLE:
        PREAMBLE();
        shadow_frame.SetVRegDouble(inst->VRegA_12x(), shadow_frame.GetVRegLong(inst->VRegB_12x()));
        inst = inst->Next_1xx();
        break;
      case Instruction::FLOAT_TO_INT: {
        PREAMBLE();
        float val = shadow_frame.GetVRegFloat(inst->VRegB_12x());
        int32_t result;
        if (val != val) {
          result = 0;
        } else if (val > static_cast<float>(kMaxInt)) {
          result = kMaxInt;
        } else if (val < static_cast<float>(kMinInt)) {
          result = kMinInt;
        } else {
          result = val;
        }
        shadow_frame.SetVReg(inst->VRegA_12x(), result);
        inst = inst->Next_1xx();
        break;
      }
      case Instruction::FLOAT_TO_LONG: {
        PREAMBLE();
        float val = shadow_frame.GetVRegFloat(inst->VRegB_12x());
        int64_t result;
        if (val != val) {
          result = 0;
        } else if (val > static_cast<float>(kMaxLong)) {
          result = kMaxLong;
        } else if (val < static_cast<float>(kMinLong)) {
          result = kMinLong;
        } else {
          result = val;
        }
        shadow_frame.SetVRegLong(inst->VRegA_12x(), result);
        inst = inst->Next_1xx();
        break;
      }
      case Instruction::FLOAT_TO_DOUBLE:
        PREAMBLE();
        shadow_frame.SetVRegDouble(inst->VRegA_12x(), shadow_frame.GetVRegFloat(inst->VRegB_12x()));
        inst = inst->Next_1xx();
        break;
      case Instruction::DOUBLE_TO_INT: {
        PREAMBLE();
        double val = shadow_frame.GetVRegDouble(inst->VRegB_12x());
        int32_t result;
        if (val != val) {
          result = 0;
        } else if (val > static_cast<double>(kMaxInt)) {
          result = kMaxInt;
        } else if (val < static_cast<double>(kMinInt)) {
          result = kMinInt;
        } else {
          result = val;
        }
        shadow_frame.SetVReg(inst->VRegA_12x(), result);
        inst = inst->Next_1xx();
        break;
      }
      case Instruction::DOUBLE_TO_LONG: {
        PREAMBLE();
        double val = shadow_frame.GetVRegDouble(inst->VRegB_12x());
        int64_t result;
        if (val != val) {
          result = 0;
        } else if (val > static_cast<double>(kMaxLong)) {
          result = kMaxLong;
        } else if (val < static_cast<double>(kMinLong)) {
          result = kMinLong;
        } else {
          result = val;
        }
        shadow_frame.SetVRegLong(inst->VRegA_12x(), result);
        inst = inst->Next_1xx();
        break;
      }
      case Instruction::DOUBLE_TO_FLOAT:
        PREAMBLE();
        shadow_frame.SetVRegFloat(inst->VRegA_12x(), shadow_frame.GetVRegDouble(inst->VRegB_12x()));
        inst = inst->Next_1xx();
        break;
      case Instruction::INT_TO_BYTE:
        PREAMBLE();
        shadow_frame.SetVReg(inst->VRegA_12x(),
                             static_cast<int8_t>(shadow_frame.GetVReg(inst->VRegB_12x())));
        inst = inst->Next_1xx();
        break;
      case Instruction::INT_TO_CHAR:
        PREAMBLE();
        shadow_frame.SetVReg(inst->VRegA_12x(),
                             static_cast<uint16_t>(shadow_frame.GetVReg(inst->VRegB_12x())));
        inst = inst->Next_1xx();
        break;
      case Instruction::INT_TO_SHORT:
        PREAMBLE();
        shadow_frame.SetVReg(inst->VRegA_12x(),
                             static_cast<int16_t>(shadow_frame.GetVReg(inst->VRegB_12x())));
        inst = inst->Next_1xx();
        break;
      case Instruction::ADD_INT:
        PREAMBLE();
        shadow_frame.SetVReg(inst->VRegA_23x(),
                             shadow_frame.GetVReg(inst->VRegB_23x()) +
                             shadow_frame.GetVReg(inst->VRegC_23x()));
        inst = inst->Next_2xx();
        break;
      case Instruction::SUB_INT:
        PREAMBLE();
        shadow_frame.SetVReg(inst->VRegA_23x(),
                             shadow_frame.GetVReg(inst->VRegB_23x()) -
                             shadow_frame.GetVReg(inst->VRegC_23x()));
        inst = inst->Next_2xx();
        break;
      case Instruction::MUL_INT:
        PREAMBLE();
        shadow_frame.SetVReg(inst->VRegA_23x(),
                             shadow_frame.GetVReg(inst->VRegB_23x()) *
                             shadow_frame.GetVReg(inst->VRegC_23x()));
        inst = inst->Next_2xx();
        break;
      case Instruction::DIV_INT: {
        PREAMBLE();
        bool success = DoIntDivide(shadow_frame, inst->VRegA_23x(),
                                   shadow_frame.GetVReg(inst->VRegB_23x()),
                                   shadow_frame.GetVReg(inst->VRegC_23x()));
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::REM_INT: {
        PREAMBLE();
        bool success = DoIntRemainder(shadow_frame, inst->VRegA_23x(),
                                      shadow_frame.GetVReg(inst->VRegB_23x()),
                                      shadow_frame.GetVReg(inst->VRegC_23x()));
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::SHL_INT:
        PREAMBLE();
        shadow_frame.SetVReg(inst->VRegA_23x(),
                             shadow_frame.GetVReg(inst->VRegB_23x()) <<
                             (shadow_frame.GetVReg(inst->VRegC_23x()) & 0x1f));
        inst = inst->Next_2xx();
        break;
      case Instruction::SHR_INT:
        PREAMBLE();
        shadow_frame.SetVReg(inst->VRegA_23x(),
                             shadow_frame.GetVReg(inst->VRegB_23x()) >>
                             (shadow_frame.GetVReg(inst->VRegC_23x()) & 0x1f));
        inst = inst->Next_2xx();
        break;
      case Instruction::USHR_INT:
        PREAMBLE();
        shadow_frame.SetVReg(inst->VRegA_23x(),
                             static_cast<uint32_t>(shadow_frame.GetVReg(inst->VRegB_23x())) >>
                             (shadow_frame.GetVReg(inst->VRegC_23x()) & 0x1f));
        inst = inst->Next_2xx();
        break;
      case Instruction::AND_INT:
        PREAMBLE();
        shadow_frame.SetVReg(inst->VRegA_23x(),
                             shadow_frame.GetVReg(inst->VRegB_23x()) &
                             shadow_frame.GetVReg(inst->VRegC_23x()));
        inst = inst->Next_2xx();
        break;
      case Instruction::OR_INT:
        PREAMBLE();
        shadow_frame.SetVReg(inst->VRegA_23x(),
                             shadow_frame.GetVReg(inst->VRegB_23x()) |
                             shadow_frame.GetVReg(inst->VRegC_23x()));
        inst = inst->Next_2xx();
        break;
      case Instruction::XOR_INT:
        PREAMBLE();
        shadow_frame.SetVReg(inst->VRegA_23x(),
                             shadow_frame.GetVReg(inst->VRegB_23x()) ^
                             shadow_frame.GetVReg(inst->VRegC_23x()));
        inst = inst->Next_2xx();
        break;
      case Instruction::ADD_LONG:
        PREAMBLE();
        shadow_frame.SetVRegLong(inst->VRegA_23x(),
                                 shadow_frame.GetVRegLong(inst->VRegB_23x()) +
                                 shadow_frame.GetVRegLong(inst->VRegC_23x()));
        inst = inst->Next_2xx();
        break;
      case Instruction::SUB_LONG:
        PREAMBLE();
        shadow_frame.SetVRegLong(inst->VRegA_23x(),
                                 shadow_frame.GetVRegLong(inst->VRegB_23x()) -
                                 shadow_frame.GetVRegLong(inst->VRegC_23x()));
        inst = inst->Next_2xx();
        break;
      case Instruction::MUL_LONG:
        PREAMBLE();
        shadow_frame.SetVRegLong(inst->VRegA_23x(),
                                 shadow_frame.GetVRegLong(inst->VRegB_23x()) *
                                 shadow_frame.GetVRegLong(inst->VRegC_23x()));
        inst = inst->Next_2xx();
        break;
      case Instruction::DIV_LONG:
        PREAMBLE();
        DoLongDivide(shadow_frame, inst->VRegA_23x(),
                     shadow_frame.GetVRegLong(inst->VRegB_23x()),
                    shadow_frame.GetVRegLong(inst->VRegC_23x()));
        POSSIBLY_HANDLE_PENDING_EXCEPTION(self->IsExceptionPending(), Next_2xx);
        break;
      case Instruction::REM_LONG:
        PREAMBLE();
        DoLongRemainder(shadow_frame, inst->VRegA_23x(),
                        shadow_frame.GetVRegLong(inst->VRegB_23x()),
                        shadow_frame.GetVRegLong(inst->VRegC_23x()));
        POSSIBLY_HANDLE_PENDING_EXCEPTION(self->IsExceptionPending(), Next_2xx);
        break;
      case Instruction::AND_LONG:
        PREAMBLE();
        shadow_frame.SetVRegLong(inst->VRegA_23x(),
                                 shadow_frame.GetVRegLong(inst->VRegB_23x()) &
                                 shadow_frame.GetVRegLong(inst->VRegC_23x()));
        inst = inst->Next_2xx();
        break;
      case Instruction::OR_LONG:
        PREAMBLE();
        shadow_frame.SetVRegLong(inst->VRegA_23x(),
                                 shadow_frame.GetVRegLong(inst->VRegB_23x()) |
                                 shadow_frame.GetVRegLong(inst->VRegC_23x()));
        inst = inst->Next_2xx();
        break;
      case Instruction::XOR_LONG:
        PREAMBLE();
        shadow_frame.SetVRegLong(inst->VRegA_23x(),
                                 shadow_frame.GetVRegLong(inst->VRegB_23x()) ^
                                 shadow_frame.GetVRegLong(inst->VRegC_23x()));
        inst = inst->Next_2xx();
        break;
      case Instruction::SHL_LONG:
        PREAMBLE();
        shadow_frame.SetVRegLong(inst->VRegA_23x(),
                                 shadow_frame.GetVRegLong(inst->VRegB_23x()) <<
                                 (shadow_frame.GetVReg(inst->VRegC_23x()) & 0x3f));
        inst = inst->Next_2xx();
        break;
      case Instruction::SHR_LONG:
        PREAMBLE();
        shadow_frame.SetVRegLong(inst->VRegA_23x(),
                                 shadow_frame.GetVRegLong(inst->VRegB_23x()) >>
                                 (shadow_frame.GetVReg(inst->VRegC_23x()) & 0x3f));
        inst = inst->Next_2xx();
        break;
      case Instruction::USHR_LONG:
        PREAMBLE();
        shadow_frame.SetVRegLong(inst->VRegA_23x(),
                                 static_cast<uint64_t>(shadow_frame.GetVRegLong(inst->VRegB_23x())) >>
                                 (shadow_frame.GetVReg(inst->VRegC_23x()) & 0x3f));
        inst = inst->Next_2xx();
        break;
      case Instruction::ADD_FLOAT:
        PREAMBLE();
        shadow_frame.SetVRegFloat(inst->VRegA_23x(),
                                  shadow_frame.GetVRegFloat(inst->VRegB_23x()) +
                                  shadow_frame.GetVRegFloat(inst->VRegC_23x()));
        inst = inst->Next_2xx();
        break;
      case Instruction::SUB_FLOAT:
        PREAMBLE();
        shadow_frame.SetVRegFloat(inst->VRegA_23x(),
                                  shadow_frame.GetVRegFloat(inst->VRegB_23x()) -
                                  shadow_frame.GetVRegFloat(inst->VRegC_23x()));
        inst = inst->Next_2xx();
        break;
      case Instruction::MUL_FLOAT:
        PREAMBLE();
        shadow_frame.SetVRegFloat(inst->VRegA_23x(),
                                  shadow_frame.GetVRegFloat(inst->VRegB_23x()) *
                                  shadow_frame.GetVRegFloat(inst->VRegC_23x()));
        inst = inst->Next_2xx();
        break;
      case Instruction::DIV_FLOAT:
        PREAMBLE();
        shadow_frame.SetVRegFloat(inst->VRegA_23x(),
                                  shadow_frame.GetVRegFloat(inst->VRegB_23x()) /
                                  shadow_frame.GetVRegFloat(inst->VRegC_23x()));
        inst = inst->Next_2xx();
        break;
      case Instruction::REM_FLOAT:
        PREAMBLE();
        shadow_frame.SetVRegFloat(inst->VRegA_23x(),
                                  fmodf(shadow_frame.GetVRegFloat(inst->VRegB_23x()),
                                        shadow_frame.GetVRegFloat(inst->VRegC_23x())));
        inst = inst->Next_2xx();
        break;
      case Instruction::ADD_DOUBLE:
        PREAMBLE();
        shadow_frame.SetVRegDouble(inst->VRegA_23x(),
                                   shadow_frame.GetVRegDouble(inst->VRegB_23x()) +
                                   shadow_frame.GetVRegDouble(inst->VRegC_23x()));
        inst = inst->Next_2xx();
        break;
      case Instruction::SUB_DOUBLE:
        PREAMBLE();
        shadow_frame.SetVRegDouble(inst->VRegA_23x(),
                                   shadow_frame.GetVRegDouble(inst->VRegB_23x()) -
                                   shadow_frame.GetVRegDouble(inst->VRegC_23x()));
        inst = inst->Next_2xx();
        break;
      case Instruction::MUL_DOUBLE:
        PREAMBLE();
        shadow_frame.SetVRegDouble(inst->VRegA_23x(),
                                   shadow_frame.GetVRegDouble(inst->VRegB_23x()) *
                                   shadow_frame.GetVRegDouble(inst->VRegC_23x()));
        inst = inst->Next_2xx();
        break;
      case Instruction::DIV_DOUBLE:
        PREAMBLE();
        shadow_frame.SetVRegDouble(inst->VRegA_23x(),
                                   shadow_frame.GetVRegDouble(inst->VRegB_23x()) /
                                   shadow_frame.GetVRegDouble(inst->VRegC_23x()));
        inst = inst->Next_2xx();
        break;
      case Instruction::REM_DOUBLE:
        PREAMBLE();
        shadow_frame.SetVRegDouble(inst->VRegA_23x(),
                                   fmod(shadow_frame.GetVRegDouble(inst->VRegB_23x()),
                                        shadow_frame.GetVRegDouble(inst->VRegC_23x())));
        inst = inst->Next_2xx();
        break;
      case Instruction::ADD_INT_2ADDR: {
        PREAMBLE();
        uint4_t vregA = inst->VRegA_12x();
        shadow_frame.SetVReg(vregA,
                             shadow_frame.GetVReg(vregA) +
                             shadow_frame.GetVReg(inst->VRegB_12x()));
        inst = inst->Next_1xx();
        break;
      }
      case Instruction::SUB_INT_2ADDR: {
        PREAMBLE();
        uint4_t vregA = inst->VRegA_12x();
        shadow_frame.SetVReg(vregA,
                             shadow_frame.GetVReg(vregA) -
                             shadow_frame.GetVReg(inst->VRegB_12x()));
        inst = inst->Next_1xx();
        break;
      }
      case Instruction::MUL_INT_2ADDR: {
        PREAMBLE();
        uint4_t vregA = inst->VRegA_12x();
        shadow_frame.SetVReg(vregA,
                             shadow_frame.GetVReg(vregA) *
                             shadow_frame.GetVReg(inst->VRegB_12x()));
        inst = inst->Next_1xx();
        break;
      }
      case Instruction::DIV_INT_2ADDR: {
        PREAMBLE();
        uint4_t vregA = inst->VRegA_12x();
        bool success = DoIntDivide(shadow_frame, vregA, shadow_frame.GetVReg(vregA),
                                   shadow_frame.GetVReg(inst->VRegB_12x()));
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_1xx);
        break;
      }
      case Instruction::REM_INT_2ADDR: {
        PREAMBLE();
        uint4_t vregA = inst->VRegA_12x();
        bool success = DoIntRemainder(shadow_frame, vregA, shadow_frame.GetVReg(vregA),
                                      shadow_frame.GetVReg(inst->VRegB_12x()));
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_1xx);
        break;
      }
      case Instruction::SHL_INT_2ADDR: {
        PREAMBLE();
        uint4_t vregA = inst->VRegA_12x();
        shadow_frame.SetVReg(vregA,
                             shadow_frame.GetVReg(vregA) <<
                             (shadow_frame.GetVReg(inst->VRegB_12x()) & 0x1f));
        inst = inst->Next_1xx();
        break;
      }
      case Instruction::SHR_INT_2ADDR: {
        PREAMBLE();
        uint4_t vregA = inst->VRegA_12x();
        shadow_frame.SetVReg(vregA,
                             shadow_frame.GetVReg(vregA) >>
                             (shadow_frame.GetVReg(inst->VRegB_12x()) & 0x1f));
        inst = inst->Next_1xx();
        break;
      }
      case Instruction::USHR_INT_2ADDR: {
        PREAMBLE();
        uint4_t vregA = inst->VRegA_12x();
        shadow_frame.SetVReg(vregA,
                             static_cast<uint32_t>(shadow_frame.GetVReg(vregA)) >>
                             (shadow_frame.GetVReg(inst->VRegB_12x()) & 0x1f));
        inst = inst->Next_1xx();
        break;
      }
      case Instruction::AND_INT_2ADDR: {
        PREAMBLE();
        uint4_t vregA = inst->VRegA_12x();
        shadow_frame.SetVReg(vregA,
                             shadow_frame.GetVReg(vregA) &
                             shadow_frame.GetVReg(inst->VRegB_12x()));
        inst = inst->Next_1xx();
        break;
      }
      case Instruction::OR_INT_2ADDR: {
        PREAMBLE();
        uint4_t vregA = inst->VRegA_12x();
        shadow_frame.SetVReg(vregA,
                             shadow_frame.GetVReg(vregA) |
                             shadow_frame.GetVReg(inst->VRegB_12x()));
        inst = inst->Next_1xx();
        break;
      }
      case Instruction::XOR_INT_2ADDR: {
        PREAMBLE();
        uint4_t vregA = inst->VRegA_12x();
        shadow_frame.SetVReg(vregA,
                             shadow_frame.GetVReg(vregA) ^
                             shadow_frame.GetVReg(inst->VRegB_12x()));
        inst = inst->Next_1xx();
        break;
      }
      case Instruction::ADD_LONG_2ADDR: {
        PREAMBLE();
        uint4_t vregA = inst->VRegA_12x();
        shadow_frame.SetVRegLong(vregA,
                                 shadow_frame.GetVRegLong(vregA) +
                                 shadow_frame.GetVRegLong(inst->VRegB_12x()));
        inst = inst->Next_1xx();
        break;
      }
      case Instruction::SUB_LONG_2ADDR: {
        PREAMBLE();
        uint4_t vregA = inst->VRegA_12x();
        shadow_frame.SetVRegLong(vregA,
                                 shadow_frame.GetVRegLong(vregA) -
                                 shadow_frame.GetVRegLong(inst->VRegB_12x()));
        inst = inst->Next_1xx();
        break;
      }
      case Instruction::MUL_LONG_2ADDR: {
        PREAMBLE();
        uint4_t vregA = inst->VRegA_12x();
        shadow_frame.SetVRegLong(vregA,
                                 shadow_frame.GetVRegLong(vregA) *
                                 shadow_frame.GetVRegLong(inst->VRegB_12x()));
        inst = inst->Next_1xx();
        break;
      }
      case Instruction::DIV_LONG_2ADDR: {
        PREAMBLE();
        uint4_t vregA = inst->VRegA_12x();
        DoLongDivide(shadow_frame, vregA, shadow_frame.GetVRegLong(vregA),
                    shadow_frame.GetVRegLong(inst->VRegB_12x()));
        POSSIBLY_HANDLE_PENDING_EXCEPTION(self->IsExceptionPending(), Next_1xx);
        break;
      }
      case Instruction::REM_LONG_2ADDR: {
        PREAMBLE();
        uint4_t vregA = inst->VRegA_12x();
        DoLongRemainder(shadow_frame, vregA, shadow_frame.GetVRegLong(vregA),
                        shadow_frame.GetVRegLong(inst->VRegB_12x()));
        POSSIBLY_HANDLE_PENDING_EXCEPTION(self->IsExceptionPending(), Next_1xx);
        break;
      }
      case Instruction::AND_LONG_2ADDR: {
        PREAMBLE();
        uint4_t vregA = inst->VRegA_12x();
        shadow_frame.SetVRegLong(vregA,
                                 shadow_frame.GetVRegLong(vregA) &
                                 shadow_frame.GetVRegLong(inst->VRegB_12x()));
        inst = inst->Next_1xx();
        break;
      }
      case Instruction::OR_LONG_2ADDR: {
        PREAMBLE();
        uint4_t vregA = inst->VRegA_12x();
        shadow_frame.SetVRegLong(vregA,
                                 shadow_frame.GetVRegLong(vregA) |
                                 shadow_frame.GetVRegLong(inst->VRegB_12x()));
        inst = inst->Next_1xx();
        break;
      }
      case Instruction::XOR_LONG_2ADDR: {
        PREAMBLE();
        uint4_t vregA = inst->VRegA_12x();
        shadow_frame.SetVRegLong(vregA,
                                 shadow_frame.GetVRegLong(vregA) ^
                                 shadow_frame.GetVRegLong(inst->VRegB_12x()));
        inst = inst->Next_1xx();
        break;
      }
      case Instruction::SHL_LONG_2ADDR: {
        PREAMBLE();
        uint4_t vregA = inst->VRegA_12x();
        shadow_frame.SetVRegLong(vregA,
                                 shadow_frame.GetVRegLong(vregA) <<
                                 (shadow_frame.GetVReg(inst->VRegB_12x()) & 0x3f));
        inst = inst->Next_1xx();
        break;
      }
      case Instruction::SHR_LONG_2ADDR: {
        PREAMBLE();
        uint4_t vregA = inst->VRegA_12x();
        shadow_frame.SetVRegLong(vregA,
                                 shadow_frame.GetVRegLong(vregA) >>
                                 (shadow_frame.GetVReg(inst->VRegB_12x()) & 0x3f));
        inst = inst->Next_1xx();
        break;
      }
      case Instruction::USHR_LONG_2ADDR: {
        PREAMBLE();
        uint4_t vregA = inst->VRegA_12x();
        shadow_frame.SetVRegLong(vregA,
                                 static_cast<uint64_t>(shadow_frame.GetVRegLong(vregA)) >>
                                 (shadow_frame.GetVReg(inst->VRegB_12x()) & 0x3f));
        inst = inst->Next_1xx();
        break;
      }
      case Instruction::ADD_FLOAT_2ADDR: {
        PREAMBLE();
        uint4_t vregA = inst->VRegA_12x();
        shadow_frame.SetVRegFloat(vregA,
                                  shadow_frame.GetVRegFloat(vregA) +
                                  shadow_frame.GetVRegFloat(inst->VRegB_12x()));
        inst = inst->Next_1xx();
        break;
      }
      case Instruction::SUB_FLOAT_2ADDR: {
        PREAMBLE();
        uint4_t vregA = inst->VRegA_12x();
        shadow_frame.SetVRegFloat(vregA,
                                  shadow_frame.GetVRegFloat(vregA) -
                                  shadow_frame.GetVRegFloat(inst->VRegB_12x()));
        inst = inst->Next_1xx();
        break;
      }
      case Instruction::MUL_FLOAT_2ADDR: {
        PREAMBLE();
        uint4_t vregA = inst->VRegA_12x();
        shadow_frame.SetVRegFloat(vregA,
                                  shadow_frame.GetVRegFloat(vregA) *
                                  shadow_frame.GetVRegFloat(inst->VRegB_12x()));
        inst = inst->Next_1xx();
        break;
      }
      case Instruction::DIV_FLOAT_2ADDR: {
        PREAMBLE();
        uint4_t vregA = inst->VRegA_12x();
        shadow_frame.SetVRegFloat(vregA,
                                  shadow_frame.GetVRegFloat(vregA) /
                                  shadow_frame.GetVRegFloat(inst->VRegB_12x()));
        inst = inst->Next_1xx();
        break;
      }
      case Instruction::REM_FLOAT_2ADDR: {
        PREAMBLE();
        uint4_t vregA = inst->VRegA_12x();
        shadow_frame.SetVRegFloat(vregA,
                                  fmodf(shadow_frame.GetVRegFloat(vregA),
                                        shadow_frame.GetVRegFloat(inst->VRegB_12x())));
        inst = inst->Next_1xx();
        break;
      }
      case Instruction::ADD_DOUBLE_2ADDR: {
        PREAMBLE();
        uint4_t vregA = inst->VRegA_12x();
        shadow_frame.SetVRegDouble(vregA,
                                   shadow_frame.GetVRegDouble(vregA) +
                                   shadow_frame.GetVRegDouble(inst->VRegB_12x()));
        inst = inst->Next_1xx();
        break;
      }
      case Instruction::SUB_DOUBLE_2ADDR: {
        PREAMBLE();
        uint4_t vregA = inst->VRegA_12x();
        shadow_frame.SetVRegDouble(vregA,
                                   shadow_frame.GetVRegDouble(vregA) -
                                   shadow_frame.GetVRegDouble(inst->VRegB_12x()));
        inst = inst->Next_1xx();
        break;
      }
      case Instruction::MUL_DOUBLE_2ADDR: {
        PREAMBLE();
        uint4_t vregA = inst->VRegA_12x();
        shadow_frame.SetVRegDouble(vregA,
                                   shadow_frame.GetVRegDouble(vregA) *
                                   shadow_frame.GetVRegDouble(inst->VRegB_12x()));
        inst = inst->Next_1xx();
        break;
      }
      case Instruction::DIV_DOUBLE_2ADDR: {
        PREAMBLE();
        uint4_t vregA = inst->VRegA_12x();
        shadow_frame.SetVRegDouble(vregA,
                                   shadow_frame.GetVRegDouble(vregA) /
                                   shadow_frame.GetVRegDouble(inst->VRegB_12x()));
        inst = inst->Next_1xx();
        break;
      }
      case Instruction::REM_DOUBLE_2ADDR: {
        PREAMBLE();
        uint4_t vregA = inst->VRegA_12x();
        shadow_frame.SetVRegDouble(vregA,
                                   fmod(shadow_frame.GetVRegDouble(vregA),
                                        shadow_frame.GetVRegDouble(inst->VRegB_12x())));
        inst = inst->Next_1xx();
        break;
      }
      case Instruction::ADD_INT_LIT16:
        PREAMBLE();
        shadow_frame.SetVReg(inst->VRegA_22s(),
                             shadow_frame.GetVReg(inst->VRegB_22s()) +
                             inst->VRegC_22s());
        inst = inst->Next_2xx();
        break;
      case Instruction::RSUB_INT:
        PREAMBLE();
        shadow_frame.SetVReg(inst->VRegA_22s(),
                             inst->VRegC_22s() -
                             shadow_frame.GetVReg(inst->VRegB_22s()));
        inst = inst->Next_2xx();
        break;
      case Instruction::MUL_INT_LIT16:
        PREAMBLE();
        shadow_frame.SetVReg(inst->VRegA_22s(),
                             shadow_frame.GetVReg(inst->VRegB_22s()) *
                             inst->VRegC_22s());
        inst = inst->Next_2xx();
        break;
      case Instruction::DIV_INT_LIT16: {
        PREAMBLE();
        bool success = DoIntDivide(shadow_frame, inst->VRegA_22s(),
                                   shadow_frame.GetVReg(inst->VRegB_22s()), inst->VRegC_22s());
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::REM_INT_LIT16: {
        PREAMBLE();
        bool success = DoIntRemainder(shadow_frame, inst->VRegA_22s(),
                                      shadow_frame.GetVReg(inst->VRegB_22s()), inst->VRegC_22s());
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::AND_INT_LIT16:
        PREAMBLE();
        shadow_frame.SetVReg(inst->VRegA_22s(),
                             shadow_frame.GetVReg(inst->VRegB_22s()) &
                             inst->VRegC_22s());
        inst = inst->Next_2xx();
        break;
      case Instruction::OR_INT_LIT16:
        PREAMBLE();
        shadow_frame.SetVReg(inst->VRegA_22s(),
                             shadow_frame.GetVReg(inst->VRegB_22s()) |
                             inst->VRegC_22s());
        inst = inst->Next_2xx();
        break;
      case Instruction::XOR_INT_LIT16:
        PREAMBLE();
        shadow_frame.SetVReg(inst->VRegA_22s(),
                             shadow_frame.GetVReg(inst->VRegB_22s()) ^
                             inst->VRegC_22s());
        inst = inst->Next_2xx();
        break;
      case Instruction::ADD_INT_LIT8:
        PREAMBLE();
        shadow_frame.SetVReg(inst->VRegA_22b(),
                             shadow_frame.GetVReg(inst->VRegB_22b()) +
                             inst->VRegC_22b());
        inst = inst->Next_2xx();
        break;
      case Instruction::RSUB_INT_LIT8:
        PREAMBLE();
        shadow_frame.SetVReg(inst->VRegA_22b(),
                             inst->VRegC_22b() -
                             shadow_frame.GetVReg(inst->VRegB_22b()));
        inst = inst->Next_2xx();
        break;
      case Instruction::MUL_INT_LIT8:
        PREAMBLE();
        shadow_frame.SetVReg(inst->VRegA_22b(),
                             shadow_frame.GetVReg(inst->VRegB_22b()) *
                             inst->VRegC_22b());
        inst = inst->Next_2xx();
        break;
      case Instruction::DIV_INT_LIT8: {
        PREAMBLE();
        bool success = DoIntDivide(shadow_frame, inst->VRegA_22b(),
                                   shadow_frame.GetVReg(inst->VRegB_22b()), inst->VRegC_22b());
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::REM_INT_LIT8: {
        PREAMBLE();
        bool success = DoIntRemainder(shadow_frame, inst->VRegA_22b(),
                                      shadow_frame.GetVReg(inst->VRegB_22b()), inst->VRegC_22b());
        POSSIBLY_HANDLE_PENDING_EXCEPTION(!success, Next_2xx);
        break;
      }
      case Instruction::AND_INT_LIT8:
        PREAMBLE();
        shadow_frame.SetVReg(inst->VRegA_22b(),
                             shadow_frame.GetVReg(inst->VRegB_22b()) &
                             inst->VRegC_22b());
        inst = inst->Next_2xx();
        break;
      case Instruction::OR_INT_LIT8:
        PREAMBLE();
        shadow_frame.SetVReg(inst->VRegA_22b(),
                             shadow_frame.GetVReg(inst->VRegB_22b()) |
                             inst->VRegC_22b());
        inst = inst->Next_2xx();
        break;
      case Instruction::XOR_INT_LIT8:
        PREAMBLE();
        shadow_frame.SetVReg(inst->VRegA_22b(),
                             shadow_frame.GetVReg(inst->VRegB_22b()) ^
                             inst->VRegC_22b());
        inst = inst->Next_2xx();
        break;
      case Instruction::SHL_INT_LIT8:
        PREAMBLE();
        shadow_frame.SetVReg(inst->VRegA_22b(),
                             shadow_frame.GetVReg(inst->VRegB_22b()) <<
                             (inst->VRegC_22b() & 0x1f));
        inst = inst->Next_2xx();
        break;
      case Instruction::SHR_INT_LIT8:
        PREAMBLE();
        shadow_frame.SetVReg(inst->VRegA_22b(),
                             shadow_frame.GetVReg(inst->VRegB_22b()) >>
                             (inst->VRegC_22b() & 0x1f));
        inst = inst->Next_2xx();
        break;
      case Instruction::USHR_INT_LIT8:
        PREAMBLE();
        shadow_frame.SetVReg(inst->VRegA_22b(),
                             static_cast<uint32_t>(shadow_frame.GetVReg(inst->VRegB_22b())) >>
                             (inst->VRegC_22b() & 0x1f));
        inst = inst->Next_2xx();
        break;
      case Instruction::UNUSED_3E ... Instruction::UNUSED_43:
      case Instruction::UNUSED_EB ... Instruction::UNUSED_FF:
      case Instruction::UNUSED_79:
      case Instruction::UNUSED_7A:
        UnexpectedOpcode(inst, mh);
    }
  }
}  // NOLINT(readability/fn_size)

// Explicit definitions of ExecuteSwitchImpl.
template JValue ExecuteSwitchImpl<true>(Thread* self, MethodHelper& mh,
                                        const DexFile::CodeItem* code_item,
                                        ShadowFrame& shadow_frame, JValue result_register);
template JValue ExecuteSwitchImpl<false>(Thread* self, MethodHelper& mh,
                                         const DexFile::CodeItem* code_item,
                                         ShadowFrame& shadow_frame, JValue result_register);

}  // namespace interpreter
}  // namespace art
