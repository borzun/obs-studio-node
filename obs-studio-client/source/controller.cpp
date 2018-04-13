// Client module for the OBS Studio node module.
// Copyright(C) 2017 Streamlabs (General Workings Inc)
// 
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110 - 1301, USA.

#include "controller.hpp"
#include "shared.hpp"
#include "utility.hpp"
#include <string>
#include <sstream>
#include <node.h>
#include <iostream>
#include <nan.h>


#pragma region Windows
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <wchar.h>

size_t spawn(std::string program, std::string commandLine, std::string workingDirectory) {
	PROCESS_INFORMATION m_win32_processInformation;
	STARTUPINFOW m_win32_startupInfo;

	// Buffers
	std::vector<wchar_t> programBuf;
	std::vector<wchar_t> commandLineBuf;
	std::vector<wchar_t> workingDirectoryBuf;

	// Convert to WideChar
	DWORD wr;
	programBuf.resize(MultiByteToWideChar(CP_UTF8, 0,
		program.data(), (int)program.size(),
		nullptr, 0) + 1);
	wr = MultiByteToWideChar(CP_UTF8, 0,
		program.data(), (int)program.size(),
		programBuf.data(), (int)programBuf.size());
	if (wr == 0) {
		// Conversion failed.
		DWORD errorCode = GetLastError();
		return false;
	}

	commandLineBuf.resize(MultiByteToWideChar(CP_UTF8, 0,
		commandLine.data(), (int)commandLine.size(),
		nullptr, 0) + 1);
	wr = MultiByteToWideChar(CP_UTF8, 0,
		commandLine.data(), (int)commandLine.size(),
		commandLineBuf.data(), (int)commandLineBuf.size());
	if (wr == 0) {
		// Conversion failed.
		DWORD errorCode = GetLastError();
		return false;
	}

	if (workingDirectory.length() > 1) {
		workingDirectoryBuf.resize(MultiByteToWideChar(CP_UTF8, 0,
			workingDirectory.data(), (int)workingDirectory.size(),
			nullptr, 0) + 1);
		if (workingDirectoryBuf.size() > 0) {
			wr = MultiByteToWideChar(CP_UTF8, 0,
				workingDirectory.data(), (int)workingDirectory.size(),
				workingDirectoryBuf.data(), (int)workingDirectoryBuf.size());
			if (wr == 0) {
				// Conversion failed.
				DWORD errorCode = GetLastError();
				return false;
			}
		}
	}

	// Build information
	memset(&m_win32_startupInfo, 0, sizeof(m_win32_startupInfo));
	memset(&m_win32_processInformation, 0, sizeof(m_win32_processInformation));

	// Launch process
	size_t attempts = 0;
	while (!CreateProcessW(
		programBuf.data(),
		commandLineBuf.data(),
		nullptr,
		nullptr,
		false,
		CREATE_NEW_CONSOLE,
		nullptr,
		workingDirectory.length() > 0 ? workingDirectoryBuf.data() : nullptr,
		&m_win32_startupInfo,
		&m_win32_processInformation)) {
		if (attempts >= 5) {
			break;
		}
		attempts++;
		std::cerr << "Attempt " << attempts << ": Creating client failed." << std::endl;
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}

	if (attempts >= 5) {
		DWORD errorCode = GetLastError();
		return false;
	}

	return (size_t)m_win32_processInformation.hProcess;
}

bool kill(size_t hProcess, uint32_t code, uint32_t& exitcode) {
	bool suc = TerminateProcess((HANDLE)hProcess, code);
	return suc;
}

std::string get_working_directory() {
	std::vector<wchar_t> bufUTF16 = std::vector<wchar_t>(65535);
	std::vector<char> bufUTF8;

	_wgetcwd(bufUTF16.data(), bufUTF16.size());

	// Convert from Wide-char to UTF8
	DWORD bufferSize = WideCharToMultiByte(CP_UTF8, 0,
		bufUTF16.data(), bufUTF16.size(),
		nullptr, 0,
		NULL, NULL);
	bufUTF8.resize(bufferSize + 1);
	DWORD finalSize = WideCharToMultiByte(CP_UTF8, 0,
		bufUTF16.data(), bufUTF16.size(),
		bufUTF8.data(), bufUTF8.size(),
		NULL, NULL);
	if (finalSize == 0) {
		// Conversion failed.
		DWORD errorCode = GetLastError();
		return false;
	}

	return bufUTF8.data();
}

#endif
#pragma endregion Windows

#pragma region JavaScript

std::string serverBinaryPath = "";
std::string serverWorkingPath = "";

void ConnectOrHost(const v8::FunctionCallbackInfo<v8::Value>& args) {
	auto isol = args.GetIsolate();
	if (args.Length() == 0) {
		isol->ThrowException(v8::Exception::SyntaxError(Nan::New<v8::String>("Too few arguments, usage: ConnectOrHost(uri).").ToLocalChecked()));
		return;
	} else if (args.Length() > 1) {
		isol->ThrowException(v8::Exception::SyntaxError(Nan::New<v8::String>("Too many arguments.").ToLocalChecked()));
		return;
	} else if (!args[0]->IsString()) {
		isol->ThrowException(v8::Exception::TypeError(Nan::New<v8::String>("Argument 'uri' must be of type 'String'.").ToLocalChecked()));
		return;
	}

	std::string uri = *v8::String::Utf8Value(args[0]);
	auto cl = Controller::GetInstance().Connect(uri);
	if (!cl) {
		cl = Controller::GetInstance().Host(uri);
		if (!cl) {
			isol->ThrowException(v8::Exception::Error(Nan::New<v8::String>("IPC failed to connect or host.").ToLocalChecked()));
			return;
		}
	}

	return;
}

void Disconnect(const v8::FunctionCallbackInfo<v8::Value>& args) {
	Controller::GetInstance().Disconnect();
}

void SetServerPath(const v8::FunctionCallbackInfo<v8::Value>& args) {
	auto isol = args.GetIsolate();
	if (args.Length() == 0) {
		isol->ThrowException(v8::Exception::SyntaxError(v8::String::NewFromUtf8(isol, "Too few arguments, usage: SetServerPath(uri).")));
		return;
	} else if (args.Length() > 1) {
		isol->ThrowException(v8::Exception::SyntaxError(v8::String::NewFromUtf8(isol, "Too many arguments.")));
		return;
	} else if (!args[0]->IsString()) {
		isol->ThrowException(v8::Exception::TypeError(v8::String::NewFromUtf8(isol, "Argument 'uri' must be of type 'String'.")));
		return;
	}

	serverBinaryPath = *v8::String::Utf8Value(args[0]);
	return;
}

void SetServerWorkingPath(const v8::FunctionCallbackInfo<v8::Value>& args) {
	auto isol = args.GetIsolate();
	if (args.Length() == 0) {
		isol->ThrowException(v8::Exception::SyntaxError(v8::String::NewFromUtf8(isol, "Too few arguments, usage: SetServerWorkingPath(uri).")));
		return;
	} else if (args.Length() > 1) {
		isol->ThrowException(v8::Exception::SyntaxError(v8::String::NewFromUtf8(isol, "Too many arguments.")));
		return;
	} else if (!args[0]->IsString()) {
		isol->ThrowException(v8::Exception::TypeError(v8::String::NewFromUtf8(isol, "Argument 'uri' must be of type 'String'.")));
		return;
	}

	serverWorkingPath = *v8::String::Utf8Value(args[0]);
	return;
}

INITIALIZER(js_ipc) {
	initializerFunctions.push([](v8::Local<v8::Object>& exports) {
		// IPC related functions will be under the IPC object.
		auto obj = v8::Object::New(exports->GetIsolate());
		NODE_SET_METHOD(obj, "ConnectOrHost", ConnectOrHost);
		NODE_SET_METHOD(obj, "Disconnect", Disconnect);
		NODE_SET_METHOD(obj, "SetServerPath", SetServerPath);
		NODE_SET_METHOD(obj, "SetServerWorkingPath", SetServerWorkingPath);
		exports->Set(v8::String::NewFromUtf8(exports->GetIsolate(), "IPC"), obj);
	});
}
#pragma endregion JavaScript

Controller::Controller() {

}

Controller::~Controller() {
	Disconnect();
}

std::shared_ptr<ipc::client> Controller::Host(std::string uri) {
	if (m_isServer)
		return nullptr;

	// Store info
	std::string program = serverBinaryPath;
	std::string commandLine = '"' + serverBinaryPath + '"' + " " + uri;
	std::string workingDirectory = serverWorkingPath.length() > 0 ? serverWorkingPath : get_working_directory();

	spawn(serverBinaryPath, commandLine, workingDirectory);
	
	// Connect
	std::shared_ptr<ipc::client> cl;
	cl = Connect(uri);
	if (!cl) { // Assume the server broke or was not allowed to run. 
		Disconnect();
		return nullptr;
	}
	
	m_isServer = true;
	return m_connection;
}

std::shared_ptr<ipc::client> Controller::Connect(std::string uri) {
	if (m_isServer)
		return nullptr;

	// Try and connect.
	std::shared_ptr<ipc::client> cl;
	for (size_t n = 0; n < 5; n++) { // Attempt 5 times.
		try {
			cl = std::make_shared<ipc::client>(uri);
			break;
		} catch (...) {
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	if (!cl) {
		return nullptr;
	}

	cl->authenticate();
	m_connection = cl;
	return m_connection;
}

void Controller::Disconnect() {
	if (m_isServer) {
		uint32_t exitcode = 0;
		kill(procId, 0, exitcode);
		m_isServer = false;
	}
	m_connection = nullptr;
}

std::shared_ptr<ipc::client> Controller::GetConnection() {
	return m_connection;
}
