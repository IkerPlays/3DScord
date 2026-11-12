/*!
 * \copy
 *     Copyright (c)  2009-2013, Cisco Systems
 *     All rights reserved.
 *
 *     Redistribution and use in source and binary forms, with or without
 *     modification, are permitted provided that the following conditions
 *     are met:
 *
 *        * Redistributions of source code must retain the above copyright
 *          notice, this list of conditions and the following disclaimer.
 *
 *        * Redistributions in binary form must reproduce the above copyright
 *          notice, this list of conditions and the following disclaimer in
 *          the documentation and/or other materials provided with the
 *          distribution.
 *
 *     THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *     "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *     LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *     FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *     COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *     INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *     BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *     LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *     CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *     LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *     ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *     POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * \file    WelsThreadLib.c
 *
 * \brief   Interfaces introduced in thread programming
 *
 * \date    11/17/2009 Created
 *
 *************************************************************************************
 */
#include "WelsThreadLib.h"

#if defined(__3DS__)

WELS_THREAD_ERROR_CODE WelsMutexInit (WELS_MUTEX* mutex) {
  LightLock_Init (mutex);
  return WELS_THREAD_ERROR_OK;
}

WELS_THREAD_ERROR_CODE WelsMutexLock (WELS_MUTEX* mutex) {
  LightLock_Lock (mutex);
  return WELS_THREAD_ERROR_OK;
}

WELS_THREAD_ERROR_CODE WelsMutexUnlock (WELS_MUTEX* mutex) {
  LightLock_Unlock (mutex);
  return WELS_THREAD_ERROR_OK;
}

WELS_THREAD_ERROR_CODE WelsMutexDestroy (WELS_MUTEX* mutex) {
  (void)mutex;
  return WELS_THREAD_ERROR_OK;
}

WELS_THREAD_ERROR_CODE WelsEventOpen (WELS_EVENT* p_event, const char* event_name) {
  (void)event_name;
  if (p_event) *p_event = 0;
  return WELS_THREAD_ERROR_GENERAL;
}

WELS_THREAD_ERROR_CODE WelsEventClose (WELS_EVENT* event, const char* event_name) {
  (void)event; (void)event_name;
  return WELS_THREAD_ERROR_OK;
}

WELS_THREAD_ERROR_CODE WelsEventSignal (WELS_EVENT* event, WELS_MUTEX* pMutex, int* iCondition) {
  (void)event; (void)pMutex; (void)iCondition;
  return WELS_THREAD_ERROR_GENERAL;
}

WELS_THREAD_ERROR_CODE WelsEventWait (WELS_EVENT* event, WELS_MUTEX* pMutex, int& iCondition) {
  (void)event; (void)pMutex; (void)iCondition;
  return WELS_THREAD_ERROR_GENERAL;
}

WELS_THREAD_ERROR_CODE WelsEventWaitWithTimeOut (WELS_EVENT* event, uint32_t dwMilliseconds, WELS_MUTEX* pMutex) {
  (void)event; (void)dwMilliseconds; (void)pMutex;
  return WELS_THREAD_ERROR_WAIT_TIMEOUT;
}

WELS_THREAD_ERROR_CODE WelsMultipleEventsWaitSingleBlocking (uint32_t nCount, WELS_EVENT* event_list,
    WELS_EVENT* master_event, WELS_MUTEX* pMutex) {
  (void)nCount; (void)event_list; (void)master_event; (void)pMutex;
  return WELS_THREAD_ERROR_GENERAL;
}

WELS_THREAD_ERROR_CODE WelsThreadCreate (WELS_THREAD_HANDLE* thread, LPWELS_THREAD_ROUTINE routine, void* arg,
    WELS_THREAD_ATTR attr) {
  (void)thread; (void)routine; (void)arg; (void)attr;
  return WELS_THREAD_ERROR_GENERAL;
}

WELS_THREAD_ERROR_CODE WelsThreadSetName (const char* thread_name) {
  (void)thread_name;
  return WELS_THREAD_ERROR_OK;
}

WELS_THREAD_ERROR_CODE WelsThreadJoin (WELS_THREAD_HANDLE thread) {
  (void)thread;
  return WELS_THREAD_ERROR_OK;
}

WELS_THREAD_HANDLE WelsThreadSelf() {
  return (WELS_THREAD_HANDLE)0;
}

WELS_THREAD_ERROR_CODE WelsQueryLogicalProcessInfo (WelsLogicalProcessInfo* pInfo) {
  pInfo->ProcessorCount = 1;
  return WELS_THREAD_ERROR_OK;
}

void WelsSleep (uint32_t dwMilliSecond) {
  svcSleepThread ((s64)dwMilliSecond * 1000000LL);
}

#endif // __3DS__
