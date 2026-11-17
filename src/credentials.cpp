#include "bridge/credentials.hpp"
#include <sodium.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dpapi.h>
#include <shlobj.h>
#endif

namespace bridge::credentials {
void validate_id(std::string_view id){
 if(id.empty()||id.size()>64)throw std::invalid_argument("Credential ID must contain 1 to 64 ASCII letters, digits, hyphens or underscores");
 for(unsigned char c:id)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'))throw std::invalid_argument("Credential ID contains an invalid character");
}
Record encode_record(const net::Key&key){Record r{};r[0]='A';r[1]='B';r[2]='K';r[3]=1;std::copy(key.begin(),key.end(),r.begin()+4);return r;}
net::Key decode_record(std::span<const uint8_t>r){if(r.size()!=36||r[0]!='A'||r[1]!='B'||r[2]!='K'||r[3]!=1)throw std::runtime_error("Unsupported or corrupt pairing credential");net::Key k;std::copy(r.begin()+4,r.end(),k.begin());return k;}
namespace {
struct WipeRecord {Record data;~WipeRecord(){sodium_memzero(data.data(),data.size());}};
#ifdef __APPLE__
struct OwnedCF {CFTypeRef value=nullptr;~OwnedCF(){if(value)CFRelease(value);}OwnedCF(const OwnedCF&)=delete;OwnedCF&operator=(const OwnedCF&)=delete;explicit OwnedCF(CFTypeRef v):value(v){}};
std::string account(std::string_view id){validate_id(id);return std::string(id);}
CFMutableDictionaryRef query(std::string_view id){
 auto name=account(id);OwnedCF cfName(CFStringCreateWithBytes(nullptr,reinterpret_cast<const UInt8*>(name.data()),name.size(),kCFStringEncodingUTF8,false));if(!cfName.value)throw std::runtime_error("Cannot allocate credential identifier");
 auto q=CFDictionaryCreateMutable(nullptr,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);if(!q)throw std::runtime_error("Cannot allocate credential query");
 CFDictionarySetValue(q,kSecClass,kSecClassGenericPassword);CFDictionarySetValue(q,kSecAttrService,CFSTR("org.audiobridge.pairing"));CFDictionarySetValue(q,kSecAttrAccount,cfName.value);return q;
}
void check(OSStatus status){if(status!=errSecSuccess)throw std::runtime_error("Keychain operation failed (OSStatus "+std::to_string(status)+")");}
#elif defined(_WIN32)
std::filesystem::path file_path(std::string_view id,const StoreOptions&o){validate_id(id);auto root=o.directory;if(root.empty()){PWSTR p=nullptr;HRESULT hr=SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_DEFAULT,nullptr,&p);if(FAILED(hr))throw std::runtime_error("Cannot locate user credential directory");root=p;CoTaskMemFree(p);root/="AudioBridge";root/="keys";}return root/(std::string(id)+".dpapi");}
struct LocalBlob {DATA_BLOB data{};~LocalBlob(){if(data.pbData){SecureZeroMemory(data.pbData,data.cbData);LocalFree(data.pbData);}}};
DATA_BLOB entropy(){static BYTE value[]="org.audiobridge.pairing.v1";return {DWORD(sizeof value-1),value};}
[[noreturn]] void winerror(const char*msg){throw std::runtime_error(std::string(msg)+" (Windows error "+std::to_string(GetLastError())+")");}
#endif
}
void store_key(std::string_view id,const net::Key&key,const StoreOptions& options){validate_id(id);WipeRecord record{encode_record(key)};
#ifdef __APPLE__
 (void)options;OwnedCF q(query(id));OwnedCF data(CFDataCreate(nullptr,record.data.data(),record.data.size()));if(!data.value)throw std::runtime_error("Cannot allocate Keychain data");
 OwnedCF attributes(CFDictionaryCreateMutable(nullptr,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks));auto a=static_cast<CFMutableDictionaryRef>(const_cast<void*>(attributes.value));CFDictionarySetValue(a,kSecValueData,data.value);
 auto status=SecItemUpdate(static_cast<CFDictionaryRef>(q.value),a);
 if(status==errSecItemNotFound){auto d=static_cast<CFMutableDictionaryRef>(const_cast<void*>(q.value));CFDictionarySetValue(d,kSecValueData,data.value);CFDictionarySetValue(d,kSecAttrLabel,CFSTR("AudioBridge pairing credential"));status=SecItemAdd(d,nullptr);}check(status);
#elif defined(_WIN32)
 auto path=file_path(id,options);DATA_BLOB input{DWORD(record.data.size()),record.data.data()},extra=entropy();LocalBlob encrypted;
 if(!CryptProtectData(&input,L"AudioBridge pairing credential",&extra,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&encrypted.data))winerror("DPAPI encryption failed");
 std::filesystem::create_directories(path.parent_path());auto random=net::random_key();auto suffix=net::key_to_hex(random).substr(0,16);sodium_memzero(random.data(),random.size());auto temporary=path;temporary+=L"."+std::wstring(suffix.begin(),suffix.end())+L".tmp";
 HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)winerror("Cannot create encrypted credential");DWORD written=0;bool success=WriteFile(file,encrypted.data.pbData,encrypted.data.cbData,&written,nullptr)&&written==encrypted.data.cbData&&FlushFileBuffers(file);DWORD saved=GetLastError();CloseHandle(file);if(!success){DeleteFileW(temporary.c_str());SetLastError(saved);winerror("Cannot write encrypted credential");}
 if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){saved=GetLastError();DeleteFileW(temporary.c_str());SetLastError(saved);winerror("Cannot commit encrypted credential");}
#else
 (void)options;throw std::runtime_error("Protected credential storage requires macOS or Windows");
#endif
}
net::Key load_key(std::string_view id,const StoreOptions&options){validate_id(id);
#ifdef __APPLE__
 (void)options;OwnedCF q(query(id));auto d=static_cast<CFMutableDictionaryRef>(const_cast<void*>(q.value));CFDictionarySetValue(d,kSecReturnData,kCFBooleanTrue);CFDictionarySetValue(d,kSecMatchLimit,kSecMatchLimitOne);CFTypeRef raw=nullptr;auto status=SecItemCopyMatching(d,&raw);OwnedCF result(raw);check(status);if(!raw||CFGetTypeID(raw)!=CFDataGetTypeID())throw std::runtime_error("Invalid Keychain credential type");auto data=static_cast<CFDataRef>(raw);return decode_record(std::span(CFDataGetBytePtr(data),size_t(CFDataGetLength(data))));
#elif defined(_WIN32)
 auto path=file_path(id,options);std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file)throw std::runtime_error("Pairing credential not found");auto size=file.tellg();if(size<=0||size>65536)throw std::runtime_error("Invalid encrypted credential size");std::vector<uint8_t> bytes(static_cast<size_t>(size));file.seekg(0);if(!file.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size())))throw std::runtime_error("Cannot read encrypted credential");DATA_BLOB input{DWORD(bytes.size()),bytes.data()},extra=entropy();LocalBlob plain;if(!CryptUnprotectData(&input,nullptr,&extra,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&plain.data))winerror("DPAPI decryption failed");return decode_record(std::span(plain.data.pbData,size_t(plain.data.cbData)));
#else
 (void)options;throw std::runtime_error("Protected credential storage requires macOS or Windows");
#endif
}
bool delete_key(std::string_view id,const StoreOptions&options){validate_id(id);
#ifdef __APPLE__
 (void)options;OwnedCF q(query(id));auto status=SecItemDelete(static_cast<CFDictionaryRef>(q.value));if(status==errSecItemNotFound)return false;check(status);return true;
#elif defined(_WIN32)
 auto path=file_path(id,options);if(DeleteFileW(path.c_str()))return true;auto e=GetLastError();if(e==ERROR_FILE_NOT_FOUND||e==ERROR_PATH_NOT_FOUND)return false;winerror("Cannot delete pairing credential");
#else
 (void)options;throw std::runtime_error("Protected credential storage requires macOS or Windows");
#endif
}
}
