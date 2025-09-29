#ifndef UNICODE
#define UNICODE
#endif

#ifndef _UNICODE
#define _UNICODE
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif

#define INITGUID

#include <initguid.h>

#include <windows.h>
#include <shlobj.h>
#include <shldisp.h>
#include <shellapi.h>
#include <objbase.h>
#include <oleauto.h>
#include <strsafe.h>
#include <stdint.h>

#ifdef _MSC_VER
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#endif

typedef struct NAMIDA_ZIP_FOOTER {
	DWORD magic;
	DWORD size;
} NAMIDA_ZIP_FOOTER;

#define NAMIDA_ZIP_MAGIC 0x3050495Au /* 'ZIP0' */

typedef struct ZIP_PAYLOAD {
	BYTE *data;
	DWORD size;
} ZIP_PAYLOAD;

static void format_system_message(DWORD code, LPWSTR buffer, size_t buffer_count) {
	if (!buffer || buffer_count == 0) {
		return;
	}

	DWORD flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
	DWORD length = FormatMessageW(flags, NULL, code, 0, buffer, (DWORD)buffer_count, NULL);
	if (length == 0) {
		StringCchPrintfW(buffer, buffer_count, L"Unknown error 0x%08lX", code);
	}
}

static void show_error_message(LPCWSTR title, LPCWSTR context, DWORD code, BOOL is_hresult) {
	wchar_t system_message[512];
	format_system_message(is_hresult ? HRESULT_CODE(code) : code, system_message, ARRAYSIZE(system_message));

	wchar_t message[768];
	if (context && *context) {
		StringCchPrintfW(message, ARRAYSIZE(message), L"%s\n\n%s (0x%08lX)", context, system_message, code);
	} else {
		StringCchPrintfW(message, ARRAYSIZE(message), L"%s (0x%08lX)", system_message, code);
	}

	MessageBoxW(NULL, message, title, MB_ICONERROR | MB_OK);
}

static BOOL read_embedded_zip(ZIP_PAYLOAD *payload, LPWSTR exe_path, size_t exe_path_count) {
	if (!payload || !exe_path) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}

	ZeroMemory(payload, sizeof(*payload));

	DWORD result = GetModuleFileNameW(NULL, exe_path, (DWORD)exe_path_count);
	if (result == 0 || result >= exe_path_count) {
		return FALSE;
	}

	HANDLE file = CreateFileW(exe_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE) {
		return FALSE;
	}

	LARGE_INTEGER file_size;
	if (!GetFileSizeEx(file, &file_size)) {
		CloseHandle(file);
		return FALSE;
	}

	if (file_size.QuadPart <= (LONGLONG)sizeof(NAMIDA_ZIP_FOOTER)) {
		CloseHandle(file);
		SetLastError(ERROR_BAD_FORMAT);
		return FALSE;
	}

	LARGE_INTEGER footer_position;
	footer_position.QuadPart = file_size.QuadPart - sizeof(NAMIDA_ZIP_FOOTER);
	if (!SetFilePointerEx(file, footer_position, NULL, FILE_BEGIN)) {
		CloseHandle(file);
		return FALSE;
	}

	NAMIDA_ZIP_FOOTER footer;
	DWORD bytes_read = 0;
	if (!ReadFile(file, &footer, sizeof(footer), &bytes_read, NULL) || bytes_read != sizeof(footer)) {
		CloseHandle(file);
		SetLastError(ERROR_READ_FAULT);
		return FALSE;
	}

	if (footer.magic != NAMIDA_ZIP_MAGIC || footer.size == 0) {
		CloseHandle(file);
		SetLastError(ERROR_BAD_FORMAT);
		return FALSE;
	}

	if ((ULONGLONG)footer.size > ((ULONGLONG)file_size.QuadPart - sizeof(NAMIDA_ZIP_FOOTER))) {
		CloseHandle(file);
		SetLastError(ERROR_BAD_FORMAT);
		return FALSE;
	}

	LARGE_INTEGER zip_position;
	zip_position.QuadPart = file_size.QuadPart - sizeof(NAMIDA_ZIP_FOOTER) - footer.size;
	if (!SetFilePointerEx(file, zip_position, NULL, FILE_BEGIN)) {
		CloseHandle(file);
		return FALSE;
	}

	BYTE *buffer = (BYTE *)HeapAlloc(GetProcessHeap(), 0, footer.size);
	if (!buffer) {
		CloseHandle(file);
		SetLastError(ERROR_OUTOFMEMORY);
		return FALSE;
	}

	if (!ReadFile(file, buffer, footer.size, &bytes_read, NULL) || bytes_read != footer.size) {
		HeapFree(GetProcessHeap(), 0, buffer);
		CloseHandle(file);
		SetLastError(ERROR_READ_FAULT);
		return FALSE;
	}

	CloseHandle(file);

	payload->data = buffer;
	payload->size = footer.size;
	return TRUE;
}

static BOOL write_buffer_to_file(LPCWSTR path, const BYTE *data, DWORD size) {
	HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE) {
		return FALSE;
	}

	DWORD bytes_written = 0;
	BOOL ok = WriteFile(file, data, size, &bytes_written, NULL);
	CloseHandle(file);

	if (!ok || bytes_written != size) {
		SetLastError(ERROR_WRITE_FAULT);
		return FALSE;
	}

	return TRUE;
}

static BOOL is_safe_relative_path(const wchar_t *relative) {
	if (!relative || !*relative) {
		return FALSE;
	}

	const wchar_t *cursor = relative;
	while (*cursor) {
		if ((cursor[0] == L'.') && (cursor[1] == L'.')) {
			wchar_t prev = (cursor > relative) ? cursor[-1] : L'\0';
			wchar_t next = cursor[2];
			if (prev == L'\\' || prev == L'/' || prev == L'\0') {
				if (next == L'\\' || next == L'/' || next == L'\0') {
					return FALSE;
				}
			}
		}
		cursor++;
	}

	return TRUE;
}

static BOOL build_destination_path(const wchar_t *zip_path,
								   const wchar_t *dest_root,
								   const wchar_t *item_path,
								   wchar_t *out_path,
								   size_t out_count) {
	if (!zip_path || !dest_root || !item_path || !out_path) {
		return FALSE;
	}

	size_t zip_len = wcslen(zip_path);
	if (_wcsnicmp(item_path, zip_path, zip_len) != 0) {
		return FALSE;
	}

	const wchar_t *relative = item_path + zip_len;
	while (*relative == L'\\' || *relative == L'/') {
		relative++;
	}

	if (!*relative) {
		return FALSE;
	}

	if (!is_safe_relative_path(relative)) {
		return FALSE;
	}

	wchar_t sanitized[1024];
	size_t index = 0;
	while (relative[index] && index < ARRAYSIZE(sanitized) - 1) {
		wchar_t ch = relative[index];
		if (ch == L'/') {
			ch = L'\\';
		}
		sanitized[index] = ch;
		index++;
	}
	sanitized[index] = L'\0';

	while (index > 0 && sanitized[index - 1] == L'\\') {
		sanitized[index - 1] = L'\0';
		index--;
	}

	if (sanitized[0] == L'\0') {
		return FALSE;
	}

	HRESULT hr = StringCchCopyW(out_path, out_count, dest_root);
	if (FAILED(hr)) {
		return FALSE;
	}

	size_t dest_len = wcslen(out_path);
	if (dest_len + 1 >= out_count) {
		return FALSE;
	}

	if (dest_len > 0 && out_path[dest_len - 1] != L'\\') {
		out_path[dest_len++] = L'\\';
		out_path[dest_len] = L'\0';
	}

	hr = StringCchCatW(out_path, out_count, sanitized);
	return SUCCEEDED(hr);
}

static void create_directory_tree(LPWSTR path, BOOL as_directory) {
	if (!path || !*path) {
		return;
	}

	wchar_t buffer[1024];
	if (FAILED(StringCchCopyW(buffer, ARRAYSIZE(buffer), path))) {
		return;
	}

	if (!as_directory) {
		wchar_t *last_separator = wcsrchr(buffer, L'\\');
		if (last_separator) {
			*last_separator = L'\0';
		}
	}

	size_t length = wcslen(buffer);
	if (length == 0) {
		return;
	}

	for (size_t i = 3; i < length; ++i) {
		if (buffer[i] == L'\\') {
			wchar_t saved = buffer[i];
			buffer[i] = L'\0';
			CreateDirectoryW(buffer, NULL);
			buffer[i] = saved;
		}
	}

	CreateDirectoryW(buffer, NULL);
}

static HRESULT prepare_destination(FolderItems *items, const wchar_t *zip_path, const wchar_t *dest_root) {
	if (!items) {
		return E_INVALIDARG;
	}

	long count = 0;
	HRESULT hr = items->lpVtbl->get_Count(items, &count);
	if (FAILED(hr)) {
		return hr;
	}

	for (long index = 0; index < count; ++index) {
		VARIANT item_index;
		VariantInit(&item_index);
		item_index.vt = VT_I4;
		item_index.lVal = index;

		FolderItem *item = NULL;
		hr = items->lpVtbl->Item(items, item_index, &item);
		VariantClear(&item_index);
		if (FAILED(hr) || !item) {
			continue;
		}

		VARIANT_BOOL is_folder = VARIANT_FALSE;
		item->lpVtbl->get_IsFolder(item, &is_folder);

		BSTR item_path_bstr = NULL;
		hr = item->lpVtbl->get_Path(item, &item_path_bstr);
		if (SUCCEEDED(hr) && item_path_bstr) {
			wchar_t destination[1024];
			if (build_destination_path(zip_path, dest_root, item_path_bstr, destination, ARRAYSIZE(destination))) {
				create_directory_tree(destination, is_folder == VARIANT_TRUE);
			}
			SysFreeString(item_path_bstr);
		}

		item->lpVtbl->Release(item);
	}

	return S_OK;
}

static HRESULT wait_for_extraction(FolderItems *items, const wchar_t *zip_path, const wchar_t *dest_root) {
	if (!items) {
		return E_INVALIDARG;
	}

	long count = 0;
	HRESULT hr = items->lpVtbl->get_Count(items, &count);
	if (FAILED(hr)) {
		return hr;
	}

	for (long index = 0; index < count; ++index) {
		VARIANT item_index;
		VariantInit(&item_index);
		item_index.vt = VT_I4;
		item_index.lVal = index;

		FolderItem *item = NULL;
		hr = items->lpVtbl->Item(items, item_index, &item);
		VariantClear(&item_index);
		if (FAILED(hr) || !item) {
			continue;
		}

		VARIANT_BOOL is_folder = VARIANT_FALSE;
		item->lpVtbl->get_IsFolder(item, &is_folder);

		if (is_folder == VARIANT_TRUE) {
			item->lpVtbl->Release(item);
			continue;
		}

		BSTR item_path_bstr = NULL;
		hr = item->lpVtbl->get_Path(item, &item_path_bstr);
		if (SUCCEEDED(hr) && item_path_bstr) {
			wchar_t destination[1024];
			if (build_destination_path(zip_path, dest_root, item_path_bstr, destination, ARRAYSIZE(destination))) {
				BOOL extracted = FALSE;
				for (int attempt = 0; attempt < 600; ++attempt) {
					DWORD attributes = GetFileAttributesW(destination);
					if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
						extracted = TRUE;
						break;
					}
					Sleep(100);
				}

				if (!extracted) {
					SysFreeString(item_path_bstr);
					item->lpVtbl->Release(item);
					return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
				}
			}
			SysFreeString(item_path_bstr);
		}

		item->lpVtbl->Release(item);
	}

	return S_OK;
}

static HRESULT extract_zip_with_shell(const wchar_t *zip_path, const wchar_t *dest_root) {
	if (!zip_path || !dest_root) {
		return E_INVALIDARG;
	}

	HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	BOOL need_uninit = FALSE;
	if (SUCCEEDED(hr)) {
		need_uninit = TRUE;
	} else if (hr == RPC_E_CHANGED_MODE) {
		hr = S_OK;
	}

	if (FAILED(hr)) {
		return hr;
	}

	IShellDispatch *shell = NULL;
	hr = CoCreateInstance(&CLSID_Shell, NULL, CLSCTX_INPROC_SERVER, &IID_IShellDispatch, (void **)&shell);
	if (FAILED(hr)) {
		if (need_uninit) {
			CoUninitialize();
		}
		return hr;
	}

	VARIANT v_zip;
	VARIANT v_dest;
	VariantInit(&v_zip);
	VariantInit(&v_dest);

	v_zip.vt = VT_BSTR;
	v_zip.bstrVal = SysAllocString(zip_path);
	if (!v_zip.bstrVal) {
		shell->lpVtbl->Release(shell);
		if (need_uninit) {
			CoUninitialize();
		}
		return E_OUTOFMEMORY;
	}

	v_dest.vt = VT_BSTR;
	v_dest.bstrVal = SysAllocString(dest_root);
	if (!v_dest.bstrVal) {
		VariantClear(&v_zip);
		shell->lpVtbl->Release(shell);
		if (need_uninit) {
			CoUninitialize();
		}
		return E_OUTOFMEMORY;
	}

	Folder *zip_folder = NULL;
	Folder *dest_folder = NULL;
	hr = shell->lpVtbl->NameSpace(shell, v_zip, &zip_folder);
	if (FAILED(hr) || !zip_folder) {
		VariantClear(&v_dest);
		VariantClear(&v_zip);
		shell->lpVtbl->Release(shell);
		if (need_uninit) {
			CoUninitialize();
		}
		return FAILED(hr) ? hr : E_FAIL;
	}

	hr = shell->lpVtbl->NameSpace(shell, v_dest, &dest_folder);
	if (FAILED(hr) || !dest_folder) {
		zip_folder->lpVtbl->Release(zip_folder);
		VariantClear(&v_dest);
		VariantClear(&v_zip);
		shell->lpVtbl->Release(shell);
		if (need_uninit) {
			CoUninitialize();
		}
		return FAILED(hr) ? hr : E_FAIL;
	}

	FolderItems *items = NULL;
	hr = zip_folder->lpVtbl->Items(zip_folder, &items);
	if (FAILED(hr) || !items) {
		dest_folder->lpVtbl->Release(dest_folder);
		zip_folder->lpVtbl->Release(zip_folder);
		VariantClear(&v_dest);
		VariantClear(&v_zip);
		shell->lpVtbl->Release(shell);
		if (need_uninit) {
			CoUninitialize();
		}
		return FAILED(hr) ? hr : E_FAIL;
	}

	hr = prepare_destination(items, zip_path, dest_root);
	if (FAILED(hr)) {
		items->lpVtbl->Release(items);
		dest_folder->lpVtbl->Release(dest_folder);
		zip_folder->lpVtbl->Release(zip_folder);
		VariantClear(&v_dest);
		VariantClear(&v_zip);
		shell->lpVtbl->Release(shell);
		if (need_uninit) {
			CoUninitialize();
		}
		return hr;
	}

	VARIANT v_items;
	VARIANT v_options;
	VariantInit(&v_items);
	VariantInit(&v_options);

	items->lpVtbl->AddRef(items);
	v_items.vt = VT_DISPATCH;
	v_items.pdispVal = (IDispatch *)items;

	v_options.vt = VT_I4;
	v_options.lVal = FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI | FOF_NOCONFIRMMKDIR;

	hr = dest_folder->lpVtbl->CopyHere(dest_folder, v_items, v_options);
	VariantClear(&v_items);
	VariantClear(&v_options);

	if (SUCCEEDED(hr)) {
		hr = wait_for_extraction(items, zip_path, dest_root);
	}

	items->lpVtbl->Release(items);
	dest_folder->lpVtbl->Release(dest_folder);
	zip_folder->lpVtbl->Release(zip_folder);
	VariantClear(&v_dest);
	VariantClear(&v_zip);
	shell->lpVtbl->Release(shell);

	if (need_uninit) {
		CoUninitialize();
	}

	return hr;
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE prev_instance, LPWSTR command_line, int show_command) {
	(void)instance;
	(void)prev_instance;
	(void)command_line;
	(void)show_command;

	wchar_t exe_path[MAX_PATH];
	ZIP_PAYLOAD payload;
	if (!read_embedded_zip(&payload, exe_path, ARRAYSIZE(exe_path))) {
		DWORD error = GetLastError();
		show_error_message(L"Namida Installer", L"Unable to locate embedded archive.", error, FALSE);
		return (int)error;
	}

	wchar_t temp_directory[MAX_PATH];
	if (GetTempPathW(ARRAYSIZE(temp_directory), temp_directory) == 0) {
		DWORD error = GetLastError();
		HeapFree(GetProcessHeap(), 0, payload.data);
		show_error_message(L"Namida Installer", L"Unable to resolve temporary directory.", error, FALSE);
		return (int)error;
	}

	wchar_t temp_zip_path[MAX_PATH];
	HRESULT hr = StringCchPrintfW(temp_zip_path, ARRAYSIZE(temp_zip_path), L"%szip.zip", temp_directory);
	if (FAILED(hr)) {
		HeapFree(GetProcessHeap(), 0, payload.data);
		show_error_message(L"Namida Installer", L"Temporary path is too long.", (DWORD)hr, TRUE);
		return (int)hr;
	}

	if (!write_buffer_to_file(temp_zip_path, payload.data, payload.size)) {
		DWORD error = GetLastError();
		HeapFree(GetProcessHeap(), 0, payload.data);
		show_error_message(L"Namida Installer", L"Unable to write temporary archive.", error, FALSE);
		return (int)error;
	}

	wchar_t program_files[MAX_PATH];
	hr = SHGetFolderPathW(NULL, CSIDL_PROGRAM_FILES, NULL, SHGFP_TYPE_CURRENT, program_files);
	if (FAILED(hr)) {
		DeleteFileW(temp_zip_path);
		HeapFree(GetProcessHeap(), 0, payload.data);
		show_error_message(L"Namida Installer", L"Unable to locate Program Files.", (DWORD)hr, TRUE);
		return (int)hr;
	}

	wchar_t destination_root[MAX_PATH];
	hr = StringCchPrintfW(destination_root, ARRAYSIZE(destination_root), L"%s\\Namida", program_files);
	if (FAILED(hr)) {
		DeleteFileW(temp_zip_path);
		HeapFree(GetProcessHeap(), 0, payload.data);
		show_error_message(L"Namida Installer", L"Installation path is too long.", (DWORD)hr, TRUE);
		return (int)hr;
	}

	if (!CreateDirectoryW(destination_root, NULL)) {
		DWORD error = GetLastError();
		if (error != ERROR_ALREADY_EXISTS) {
			DeleteFileW(temp_zip_path);
			HeapFree(GetProcessHeap(), 0, payload.data);
			show_error_message(L"Namida Installer", L"Unable to create destination folder.", error, FALSE);
			return (int)error;
		}
	}

	hr = extract_zip_with_shell(temp_zip_path, destination_root);

	DeleteFileW(temp_zip_path);
	HeapFree(GetProcessHeap(), 0, payload.data);

	if (FAILED(hr)) {
		show_error_message(L"Namida Installer", L"Unable to extract archive.", (DWORD)hr, TRUE);
		return (int)hr;
	}

	MessageBoxW(NULL,
				L"Namida for Windows XP has been installed into Program Files.",
				L"Namida Installer",
				MB_ICONINFORMATION | MB_OK);

	return 0;
}
