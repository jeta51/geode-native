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

#include "IpcHandler.hpp"

#include <errno.h>
#include <memory.h>

#include <fwklib/FwkLog.hpp>

#include <ace/INET_Addr.h>
#include <ace/OS.h>
#include <ace/SOCK_Connector.h>
#include <ace/SOCK_IO.h>

namespace apache {
namespace geode {
namespace client {
namespace testframework {

// This class is not thread safe, and its use in the test framework
// does not require it to be.
IpcHandler::IpcHandler(const ACE_INET_Addr &driver, int32_t waitSeconds)
    : m_io(new ACE_SOCK_Stream()) {
  ACE_OS::signal(SIGPIPE, SIG_IGN);  // Ignore broken pipe
  ACE_SOCK_Connector conn;
  ACE_Time_Value wtime(waitSeconds, 1000);
  int32_t retVal = conn.connect(*m_io, driver, &wtime);
  if (retVal == -1) {
#ifdef WIN32
    errno = WSAGetLastError();
#endif
    FWKEXCEPTION("Attempt to connect failed, error: " << errno);
  }
}

IpcHandler::~IpcHandler() {
  sendIpcMsg(IPC_EXITING);
  close();
  checkBuffer(-12);
}

void IpcHandler::close() {
  if (m_io) {
    m_io->close();
    delete m_io;
    m_io = nullptr;
  }
}

bool IpcHandler::checkPipe() {
  static ACE_Time_Value next;
  if (!m_io) {
    return false;
  }
  ACE_Time_Value now = ACE_OS::gettimeofday();
  if (next < now) {
    ACE_Time_Value interval(60);
    next = now + interval;
    if (!sendIpcMsg(IPC_NULL)) return false;
  }
  return true;
}

IpcMsg IpcHandler::readIpcMsg(int32_t waitSeconds) {
  IpcMsg result = IPC_NULL;
  int32_t red = readInt(waitSeconds);
  if (red != -1) {
    result = static_cast<IpcMsg>(red);
    if (result == IPC_NULL) {
      return readIpcMsg(waitSeconds);  // skip past nulls
    }
  }
  return result;
}

int32_t IpcHandler::readInt(int32_t waitSeconds) {
  int32_t result = -1;
  if (!checkPipe()) {
    FWKEXCEPTION("Connection failure, error: " << errno);
  }

  auto wtime = new ACE_Time_Value(waitSeconds, 1000);
  int32_t redInt = -1;
  auto length = m_io->recv_n(&redInt, 4, 0, wtime);
  delete wtime;
  if (length == 4) {
    result = ntohl(redInt);
    //    FWKDEBUG( "Received " << result );
  } else {
    if (length == -1) {
#ifdef WIN32
      errno = WSAGetLastError();
#endif
      if ((errno > 0) && (errno != ETIME)) {
        FWKEXCEPTION("Read failure, error: " << errno);
      }
    }
  }
  return result;
}

char *IpcHandler::checkBuffer(int32_t len) {
  static int32_t length = 512;
  static char *buffer = nullptr;

  if (len == -12) {
    delete[] buffer;
    buffer = nullptr;
    return buffer;
  }

  if (length < len) {
    length = len + 32;
    if (buffer != nullptr) {
      delete[] buffer;
      buffer = nullptr;
    }
  }

  if (buffer == nullptr) {
    buffer = new char[length];
  }

  memset(buffer, 0, length);
  return buffer;
}

std::string IpcHandler::readString(int32_t waitSeconds) {
  int32_t length = readInt(waitSeconds);
  if (length == -1) {
    FWKEXCEPTION(
        "Failed to read string, length not available, errno: " << errno);
  }

  char *buffer = checkBuffer(length);

  ACE_Time_Value *wtime = new ACE_Time_Value(waitSeconds, 1000);

  auto readLength = m_io->recv(buffer, length, 0, wtime);
  delete wtime;
  if (readLength <= 0) {
    if (readLength < 0) {
#ifdef WIN32
      errno = WSAGetLastError();
#endif
    } else {
      errno = EWOULDBLOCK;
    }
    FWKEXCEPTION("Failed to read string from socket, errno: " << errno);
  }
  //  FWKDEBUG( "Received " << buffer );
  return buffer;
}

IpcMsg IpcHandler::getIpcMsg(int32_t waitSeconds, std::string &str) {
  IpcMsg msg = readIpcMsg(waitSeconds);
  switch (msg) {
    case IPC_PING:
    case IPC_NULL:  // null should never be seen here
    case IPC_ACK:
    case IPC_EXITING:
      break;  // nothing required
    case IPC_DONE:
    case IPC_RUN:  // Need to read the rest
      str = readString(waitSeconds);
      if (!str.empty()) sendIpcMsg(IPC_ACK);
      break;
    case IPC_ERROR:
    case IPC_EXIT:
      sendIpcMsg(IPC_ACK);
      break;
  }
  return msg;
}

IpcMsg IpcHandler::getIpcMsg(int32_t waitSeconds) {
  IpcMsg msg = readIpcMsg(waitSeconds);
  switch (msg) {
    case IPC_PING:
    case IPC_NULL:  // null should never be seen here
    case IPC_ACK:
    case IPC_EXITING:
      break;  // nothing required
    case IPC_DONE:
    case IPC_RUN:  // Need to read the rest
      break;
    case IPC_ERROR:
    case IPC_EXIT:
      sendIpcMsg(IPC_ACK);
      break;
  }
  return msg;
}

bool IpcHandler::sendIpcMsg(IpcMsg msg, int32_t waitSeconds) {
  int32_t writeInt = htonl(msg);
  //  FWKDEBUG( "Sending " << ( int32_t )msg );
  ACE_Time_Value tv(waitSeconds, 1000);
  auto wrote = m_io->send(&writeInt, 4, &tv);
  if (wrote == -1) {
#ifdef WIN32
    errno = WSAGetLastError();
#endif
    if (errno > 0) {
      FWKEXCEPTION("Send failure, error: " << errno);
    }
  }
  if (wrote == 4) {
    switch (msg) {
      case IPC_NULL:
      case IPC_ERROR:
      case IPC_ACK:
      case IPC_RUN:
      case IPC_DONE:
        return true;
      case IPC_EXITING:
      case IPC_PING:
      case IPC_EXIT:
        msg = getIpcMsg(60);
        if (msg == IPC_ACK) return true;
        break;
    }
  }
  return false;
}

bool IpcHandler::sendBuffer(IpcMsg msg, const char *str) {
  auto length = static_cast<int32_t>(strlen(str));
  char *buffer = checkBuffer(length);
  *reinterpret_cast<IpcMsg *>(buffer) = static_cast<IpcMsg>(htonl(msg));
  *reinterpret_cast<int32_t *>(buffer + 4) = htonl(length);
  strncpy((buffer + 8), str, length - 8);

  //  FWKDEBUG( "Sending " << ( int32_t )msg << "  and string: " << str );
  length += 8;
  auto wrote = m_io->send(buffer, length);
  if (wrote == -1) {
#ifdef WIN32
    errno = WSAGetLastError();
#endif
    if (errno > 0) {
      FWKEXCEPTION("Send failure, error: " << errno);
    }
  }
  if (wrote == length) {
    if (getIpcMsg(180) != IPC_ACK) {
      FWKEXCEPTION("Send was not ACK'ed.");
    }
    return true;
  }
  FWKEXCEPTION("Tried to write " << length << " bytes, only wrote " << wrote);
}

}  // namespace testframework
}  // namespace client
}  // namespace geode
}  // namespace apache
