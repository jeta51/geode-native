/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#ifndef GEODE_INTEGRATION_TEST_THINCLIENTPUTALLTIMEOUT_H_
#define GEODE_INTEGRATION_TEST_THINCLIENTPUTALLTIMEOUT_H_

#include "fw_dunit.hpp"
#include "ThinClientHelper.hpp"
#include "TallyListener.hpp"
#include "TallyWriter.hpp"

#define CLIENT1 s1p1
#define CLIENT2 s1p2
#define SERVER1 s2p1

namespace { // NOLINT(google-build-namespaces)

using apache::geode::client::Exception;
using apache::geode::client::HashMapOfCacheable;
using apache::geode::client::TimeoutException;

using apache::geode::client::testing::TallyListener;
using apache::geode::client::testing::TallyWriter;

bool isLocalServer = true;
static bool isLocator = false;
const char* locatorsG =
    CacheHelper::getLocatorHostPort(isLocator, isLocalServer, 1);
#include "LocatorHelper.hpp"

std::shared_ptr<TallyListener> reg1Listener1;
std::shared_ptr<TallyWriter> reg1Writer1;

int numCreates = 0;
int numUpdates = 0;
int numInvalidates = 0;
int numDestroys = 0;

void setCacheListener(const char* regName,
                      std::shared_ptr<TallyListener> regListener) {
  auto reg = getHelper()->getRegion(regName);
  auto attrMutator = reg->getAttributesMutator();
  attrMutator->setCacheListener(regListener);
}

void setCacheWriter(const char* regName,
                    std::shared_ptr<TallyWriter> regWriter) {
  auto reg = getHelper()->getRegion(regName);
  auto attrMutator = reg->getAttributesMutator();
  attrMutator->setCacheWriter(regWriter);
}

void validateEventCount(int line) {
  LOGINFO("ValidateEvents called from line (%d).", line);
  int num = reg1Listener1->getCreates();
  char buf[1024];
  sprintf(buf, "Got wrong number of creation events. expected[%d], real[%d]",
          numCreates, num);
  ASSERT(num == numCreates, buf);
  num = reg1Listener1->getUpdates();
  sprintf(buf, "Got wrong number of update events. expected[%d], real[%d]",
          numUpdates, num);
  ASSERT(num == numUpdates, buf);
  num = reg1Writer1->getCreates();
  sprintf(buf, "Got wrong number of writer events. expected[%d], real[%d]",
          numCreates, num);
  ASSERT(num == numCreates, buf);
  num = reg1Listener1->getInvalidates();
  sprintf(buf, "Got wrong number of invalidate events. expected[%d], real[%d]",
          numInvalidates, num);
  ASSERT(num == numInvalidates, buf);
  num = reg1Listener1->getDestroys();
  sprintf(buf, "Got wrong number of destroys events. expected[%d], real[%d]",
          numDestroys, num);
  ASSERT(num == numDestroys, buf);
}

DUNIT_TASK_DEFINITION(SERVER1, StartServer)
  {
    if (isLocalServer) {
      CacheHelper::initServer(
          1, "cacheserver_notify_subscription_PutAllTimeout.xml");
    }
    LOG("SERVER started");
  }
END_TASK_DEFINITION

DUNIT_TASK_DEFINITION(CLIENT1, SetupClient1_Pool_Locator)
  {
    initClient(true);
    createPooledRegion(regionNames[0], false /*ack mode*/, locatorsG,
                       "__TEST_POOL1__", true /*client notification*/);
  }
END_TASK_DEFINITION

void putAllWithOneEntryTimeout(std::chrono::milliseconds timeout,
                               std::chrono::milliseconds waitTimeOnServer) {
  LOG("Do large PutAll");
  HashMapOfCacheable map0;
  map0.clear();

  for (int i = 0; i < 100000; i++) {
    char key0[50] = {0};
    char val0[2500] = {0};
    sprintf(key0, "key-%d", i);
    sprintf(val0, "%1000d", i);
    map0.emplace(CacheableKey::create(key0), CacheableString::create(val0));
  }

  map0.emplace(CacheableKey::create("timeout-this-entry"),
               CacheableString::create(
                   std::to_string(waitTimeOnServer.count()).c_str()));

  auto regPtr0 = getHelper()->getRegion(regionNames[0]);

  regPtr0->putAll(map0, timeout);
}

void putAllWithOneEntryTimeoutWithCallBackArg(
    std::chrono::milliseconds timeout,
    std::chrono::milliseconds waitTimeOnServer) {
  LOG("Do large PutAll putAllWithOneEntryTimeoutWithCallBackArg");
  HashMapOfCacheable map0;
  map0.clear();

  for (int i = 0; i < 100000; i++) {
    char key0[50] = {0};
    char val0[2500] = {0};
    sprintf(key0, "key-%d", i);
    sprintf(val0, "%1000d", i);
    map0.emplace(CacheableKey::create(key0), CacheableString::create(val0));
  }

  map0.emplace(CacheableKey::create("timeout-this-entry"),
               CacheableString::create(
                   std::to_string(waitTimeOnServer.count()).c_str()));

  auto regPtr0 = getHelper()->getRegion(regionNames[0]);

  regPtr0->putAll(map0, timeout, CacheableInt32::create(1000));
  LOG("Do large PutAll putAllWithOneEntryTimeoutWithCallBackArg complete. ");
}

DUNIT_TASK_DEFINITION(CLIENT1, testTimeoutException)
  {
    printf("start task testTimeoutException\n");
    auto regPtr = getHelper()->getRegion(regionNames[0]);

    regPtr->registerAllKeys();

    try {
      putAllWithOneEntryTimeout(std::chrono::seconds(20),
                                std::chrono::seconds(40));
      FAIL("Didnt get expected timeout exception for putAll");
    } catch (const TimeoutException& excp) {
      std::string logmsg = "";
      logmsg += "PutAll expected timeout exception ";
      logmsg += excp.getName();
      logmsg += ": ";
      logmsg += excp.what();
      LOG(logmsg.c_str());
    }
    dunit::sleep(30000);

    try {
      putAllWithOneEntryTimeoutWithCallBackArg(std::chrono::seconds(20),
                                               std::chrono::seconds(40));
      FAIL("Didnt get expected timeout exception for putAllwithCallBackArg");
    } catch (const TimeoutException& excp) {
      std::string logmsg = "";
      logmsg += "PutAll with CallBackArg expected timeout exception ";
      logmsg += excp.getName();
      logmsg += ": ";
      logmsg += excp.what();
      LOG(logmsg.c_str());
    }

    dunit::sleep(30000);
    LOG("testTimeoutException completed");
  }
END_TASK_DEFINITION

DUNIT_TASK_DEFINITION(CLIENT1, testWithoutTimeoutException)
  {
    printf("start task testWithoutTimeoutException\n");
    auto regPtr = getHelper()->getRegion(regionNames[0]);

    // regPtr->registerAllKeys();

    try {
      putAllWithOneEntryTimeout(std::chrono::seconds(40),
                                std::chrono::seconds(20));
      LOG("testWithoutTimeoutException completed");
      return;
    } catch (const TimeoutException& excp) {
      std::string logmsg = "";
      logmsg += "Not expected timeout exception ";
      logmsg += excp.getName();
      logmsg += ": ";
      logmsg += excp.what();
      LOG(logmsg.c_str());
    } catch (const Exception& ex) {
      printf("Exception while putALL :: %s : %s\n", ex.getName().c_str(),
             ex.what());
    }
    FAIL("Something is wrong while putAll");
  }
END_TASK_DEFINITION

DUNIT_TASK_DEFINITION(CLIENT1, testWithoutTimeoutWithCallBackArgException)
  {
    try {
      putAllWithOneEntryTimeoutWithCallBackArg(std::chrono::seconds(40),
                                               std::chrono::seconds(20));
      LOG("testWithoutTimeoutException completed");
      return;
    } catch (const TimeoutException& excp) {
      std::string logmsg = "";
      logmsg += "Not expected timeout exception ";
      logmsg += excp.getName();
      logmsg += ": ";
      logmsg += excp.what();
      LOG(logmsg.c_str());
    } catch (const Exception& ex) {
      printf(
          "Exception while putAllWithOneEntryTimeoutWithCallBackArg :: %s : "
          "%s\n",
          ex.getName().c_str(), ex.what());
    }
    FAIL("Something is wrong while putAllWithOneEntryTimeoutWithCallBackArg");
  }
END_TASK_DEFINITION

DUNIT_TASK_DEFINITION(CLIENT1, StopClient1)
  {
    cleanProc();
    LOG("CLIENT1 stopped");
  }
END_TASK_DEFINITION

DUNIT_TASK_DEFINITION(SERVER1, StopServer)
  {
    if (isLocalServer) CacheHelper::closeServer(1);
    LOG("SERVER stopped");
  }
END_TASK_DEFINITION

}  // namespace

#endif  // GEODE_INTEGRATION_TEST_THINCLIENTPUTALLTIMEOUT_H_
