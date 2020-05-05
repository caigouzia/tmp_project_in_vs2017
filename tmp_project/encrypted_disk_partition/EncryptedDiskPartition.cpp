﻿#include "EncryptedDiskPartition.h"
#include "sm4/sm4.h"

#pragma warning(disable:4996)

EncryptedDiskPartition::EncryptedDiskPartition() {
    cout << "create EncryptedDiskPartition object" << endl;
}

EncryptedDiskPartition::~EncryptedDiskPartition() {
    cout << "delete EncryptedDiskPartition object" << endl;
}

// 判断分区类型
VOID EncryptedDiskPartition::JudgePartitionType(const int device_number) {
    UCHAR data[8] = {0};
    ReadPhysicalSector(1, sizeof(data), data, sizeof(data), device_number);
    cout << *data << endl;
    const string tag_flag = "EFI";
    char verify[8] = {0};

    for (auto i = 0; i < tag_flag.length(); ++i)
        sprintf_s(verify + i, 2, "%c", data[i]);
    if (!strcmp(tag_flag.c_str(), verify)) {
        cout << "This is GPT" << endl;
        EncryptGpt(device_number);
    } else {
        cout << "This is MBR" << endl;
        EncryptMbr(device_number);
    }
}


BOOL EncryptedDiskPartition::EncryptMbr(const int device_number) {
    UCHAR data[513] = {0};
    ReadPhysicalSector(0, sizeof(data), data, sizeof(data), device_number);

    UCHAR law_data[65] = {0};
    // 复制原始数据
    memcpy_s(law_data, 64, data + 0x1B0 + 0x0E, 64);
    cout << law_data << endl;

    // 保留备份
    WritePhysicalSector(35, 64, law_data, 0x1B0 + 0x0E, device_number);

    byte* keys = new byte[32];
    char* key = (char*)"0123456789ABCDEFFEDCBA9876543210";

    byte* plain_text = new byte[65];

    memset(plain_text, 0, 65 * sizeof(BYTE));


    unsigned long t = strlen(key);

    hexCharStr2unsignedCharStr(key, strlen(key), 0, keys, &t);

    BYTE cipher_text[130];
    char cipher_string[130];

    sm4_context ctx;
    unsigned long i;
    sm4_setkey_enc(&ctx, keys);
    sm4_crypt_ecb(&ctx, 64, law_data, cipher_text);

    // 保留备份
    WritePhysicalSector(PartitionEncryptedDataSector, 64, cipher_text, 0x00, device_number);

    // memset(law_data, 0, sizeof(law_data));
    // 删除原来的分区表
    // WritePhysicalSector(0, 64, law_data, 0x1B0 + 0x0E, device_number);

    return false;
}



BOOL EncryptedDiskPartition::EncryptGpt(const int device_number) const {
    UCHAR data[513] = {0};
    ReadPhysicalSector(1, sizeof(data), data, sizeof(data), device_number);
    auto lba_backup_sector_int = 0;
    string lba_backup_sector_str;
    for (auto i = 0x32; i >= 0x30; --i) {
        char tmp[5] = {0 } ;
        sprintf_s(tmp, 4, "%02X", data[i]);
        lba_backup_sector_str += tmp;
    }
    lba_backup_sector_int = strtol(lba_backup_sector_str.c_str(), nullptr, 16);
    cout << "备份LBA 地址:" << lba_backup_sector_int << endl;
    int partitoin_number = 0;
    for (int i = 0; i < 128; ++i) {
        UCHAR tmp[513] = {0};
        ReadPhysicalSector(2 + i / 4, sizeof(tmp), tmp, sizeof(tmp),
                           device_number);
        UCHAR header[8] = {0};
        memcpy_s(header, 8, tmp + 128 * (i % 4), 8);
        if (header[0] != '\0') {
            cout << "this is partition" << endl;
            partitoin_number += 1;
        }
    }
    cout << "分区数:" << partitoin_number << endl;

    UCHAR law_data[BytesPerSector * 32] = {0};
    ReadPhysicalSectorWithoutCutOff(2, sizeof(law_data), law_data,
                                    sizeof(law_data),
                                    device_number);

    // 保留备份
    WritePhysicalSectorWithoutOffset(35, sizeof(law_data), law_data, 0x00,
                                     device_number);

    return 0;
}


BOOL EncryptedDiskPartition::DecryptMbr(const int device_number) {
    UCHAR data[513] = {0};
    ReadPhysicalSector(PartitionEncryptedDataSector, sizeof(data), data, sizeof(data), device_number);

    UCHAR encrypt_data[65] = {0};
    // for (auto i = 0x1B0 + 0x0E; i < 0x1FF; i += 1)
    memcpy_s(encrypt_data, 64, data + 0x00, 64);
    cout << encrypt_data << endl;
    byte* keys = new byte[32];
    char* key = (char*)"0123456789ABCDEFFEDCBA9876543210";

    byte* plain_text = new byte[65];

    memset(plain_text, 0, 65 * sizeof(BYTE));
    unsigned long t = strlen(key);

    hexCharStr2unsignedCharStr(key, strlen(key), 0, keys, &t);

    sm4_context ctx;
    unsigned long i;
    sm4_setkey_dec(&ctx, keys);
    sm4_crypt_ecb(&ctx, 64, encrypt_data, plain_text);

    WritePhysicalSector(0, 64, plain_text, 0x1B0 + 0x0E, device_number);

    return false;
}


BOOL EncryptedDiskPartition::DecryptGpt(const int device_number) const {
    UCHAR law_data[BytesPerSector * 32] = {0};
    ReadPhysicalSectorWithoutCutOff(35, sizeof(law_data), law_data,
                                    sizeof(law_data),
                                    device_number);

    // 保留备份
    WritePhysicalSectorWithoutOffset(2, sizeof(law_data), law_data, 0x00,
                                     device_number);

    return false;
}


BOOL EncryptedDiskPartition::ReadPhysicalSector(const LONGLONG sector_start,
        const ULONG byte_counts,
        UCHAR* output_buffer,
        const ULONG buffer_size,
        const int device_number) const {
    ULONG n_bytes;
    auto result = FALSE;
    HANDLE h_device_handle = NULL;
    wchar_t drive[32] = {0};  // L"\\\\.\\PHYSICALDRIVE3";
    wsprintf(drive, L"\\\\.\\PHYSICALDRIVE%d", device_number);
    //#define WIN32_LEAN_AND_MEAN

    UCHAR data[1024] = {0};

    h_device_handle =
        CreateFile(drive, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                   OPEN_EXISTING, 0, 0);

    if (h_device_handle) {
        LARGE_INTEGER pointer;
        pointer.QuadPart = sector_start;
        pointer.QuadPart *= BytesPerSector;

        SetFilePointer(h_device_handle, pointer.LowPart, &pointer.HighPart,
                       FILE_BEGIN);
        const auto handle_r =
            ReadFile(h_device_handle, data, BytesPerSector, &n_bytes, NULL);
        if (handle_r) {
            result = TRUE;
            // strncpy_s(reinterpret_cast<char*>(output_buffer), buffer_size,
            // reinterpret_cast<char*>(data), byte_counts);
            memcpy_s(reinterpret_cast<char*>(output_buffer), buffer_size,
                     reinterpret_cast<char*>(data), byte_counts);
        }
        // if (ReadFile(hDeviceHandle, p, SectorCount*BytesPerSector, &nBytes,
        // NULL))
        else
            cout << ("read error %d\n", GetLastError());

        CloseHandle(h_device_handle);
    } else
        cout << ("open error %d\n", GetLastError());
    return result;
}

BOOL EncryptedDiskPartition::WritePhysicalSector(const LONGLONG sector_start,
        const ULONG byte_counts,
        UCHAR* input_buffer,
        const ULONG offset,
        const int device_number) const {
    ULONG n_bytes;
    auto result = FALSE;
    HANDLE h_device_handle = NULL;
    wchar_t drive[32] = {0};  // L"\\\\.\\PHYSICALDRIVE3";
    wsprintf(drive, L"\\\\.\\PHYSICALDRIVE%d", device_number);
    //#define WIN32_LEAN_AND_MEAN


    UCHAR data[BytesPerSector] = {0};

    // 保留偏移前的数据
    ReadPhysicalSector(sector_start, sizeof(data), data, sizeof(data), device_number);
    memcpy(reinterpret_cast<char*>(data) + offset,
           reinterpret_cast<char*>(input_buffer),
           byte_counts);

    h_device_handle =
        CreateFile(drive, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                   OPEN_EXISTING, 0, 0);

    if (h_device_handle) {
        LARGE_INTEGER pointer;
        pointer.QuadPart = sector_start;
        pointer.QuadPart *= BytesPerSector;

        SetFilePointer(h_device_handle, pointer.LowPart, &pointer.HighPart,
                       FILE_BEGIN);
        // const auto handle_r = ReadFile(h_device_handle, data, BytesPerSector,
        // &n_bytes, NULL);

        const auto handle_r = WriteFile(h_device_handle, static_cast<LPVOID>(data),
                                        BytesPerSector, &n_bytes, NULL);
        FlushFileBuffers(h_device_handle);
        if (handle_r)
            result = TRUE;
        else
            cout << ("read error %d\n", GetLastError());
        CloseHandle(h_device_handle);
    } else
        cout << ("open error %d\n", GetLastError());
    return result;
}

BOOL EncryptedDiskPartition::ReadPhysicalSectorWithoutCutOff(const LONGLONG sector_start,
        const ULONG byte_counts,
        UCHAR* output_buffer,
        const ULONG buffer_size,
        const int device_number) const {
    ULONG n_bytes;
    auto result = FALSE;
    HANDLE h_device_handle = NULL;
    wchar_t drive[32] = {0};  // L"\\\\.\\PHYSICALDRIVE3";
    wsprintf(drive, L"\\\\.\\PHYSICALDRIVE%d", device_number);
    //#define WIN32_LEAN_AND_MEAN

    // UCHAR data[1024] = {0};

    h_device_handle =
        CreateFile(drive, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                   OPEN_EXISTING, 0, 0);

    if (h_device_handle) {
        LARGE_INTEGER pointer;
        pointer.QuadPart = sector_start;
        pointer.QuadPart *= BytesPerSector;

        SetFilePointer(h_device_handle, pointer.LowPart, &pointer.HighPart,
                       FILE_BEGIN);
        const auto handle_r = ReadFile(h_device_handle, output_buffer,
                                       BytesPerSector, &n_bytes, NULL);
        if (handle_r) {
            result = TRUE;

            // memcpy_s(reinterpret_cast<char*>(output_buffer), buffer_size,
            //          reinterpret_cast<char*>(data), byte_counts);
        }
        // if (ReadFile(hDeviceHandle, p, SectorCount*BytesPerSector, &nBytes,
        // NULL))
        else
            cout << ("read error %d\n", GetLastError());

        CloseHandle(h_device_handle);
    } else
        cout << ("open error %d\n", GetLastError());
    return result;
}

BOOL EncryptedDiskPartition::WritePhysicalSectorWithoutOffset(
    const LONGLONG sector_start, const ULONG byte_counts, UCHAR* input_buffer,
    const ULONG offset, const int device_number) const {
    ULONG n_bytes;
    auto result = FALSE;
    HANDLE h_device_handle = NULL;
    wchar_t drive[32] = {0};  // L"\\\\.\\PHYSICALDRIVE3";
    wsprintf(drive, L"\\\\.\\PHYSICALDRIVE%d", device_number);
    //#define WIN32_LEAN_AND_MEAN


    h_device_handle =
        CreateFile(drive, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                   OPEN_EXISTING, 0, 0);

    if (h_device_handle) {
        LARGE_INTEGER pointer;
        pointer.QuadPart = sector_start;
        pointer.QuadPart *= BytesPerSector;

        SetFilePointer(h_device_handle, pointer.LowPart, &pointer.HighPart,
                       FILE_BEGIN);
        // const auto handle_r = ReadFile(h_device_handle, data, BytesPerSector,
        // &n_bytes, NULL);

        const auto handle_r =
            WriteFile(h_device_handle, static_cast<LPVOID>(input_buffer),
                      byte_counts, &n_bytes, NULL);
        FlushFileBuffers(h_device_handle);
        if (handle_r)
            result = TRUE;
        else
            cout << ("read error %d\n", GetLastError());
        CloseHandle(h_device_handle);
    } else
        cout << ("open error %d\n", GetLastError());
    return result;
}



// 将16进制的string字符串，转成16进制的arr
int EncryptedDiskPartition::hexCharStr2unsignedCharStr(char* src,
        unsigned long lsrc,
        int flag,
        unsigned char* out, unsigned long* lout) {
    if ((0 == flag && 0 != lsrc % 2) || (0 != flag && 0 != lsrc % 3) ||
            NULL == src || NULL == out) {
        if ((0 == flag && 0 != lsrc % 2)) printf("长度有问题,lsrc = %d\n", lsrc);
        if ((0 != flag && 0 != lsrc % 3)) printf("3的倍数？\n");
        if (NULL == src) printf("src是空的 \n");
        if (NULL == out) printf("lout是空的 \n");
        printf("error 1 \n");
        return 1;  // param err
    }

    int j = 0;  // index of out buff
    if (0 == flag) {
        // int i;
        for (int i = 0; i < lsrc; i += 2) {
            int tmp = 0;
            int HIGH_HALF_BYTE = 0;
            int LOW_HALF_BYTE = 0;
            if (src[i] >= 0x30 && src[i] <= 0x39)
                HIGH_HALF_BYTE = src[i] - 0x30;
            else if (src[i] >= 0x41 && src[i] <= 0x46)
                HIGH_HALF_BYTE = src[i] - 0x37;
            else if (src[i] >= 0x61 && src[i] <= 0x66)
                HIGH_HALF_BYTE = src[i] - 0x57;
            else if (src[i] == 0x20)
                HIGH_HALF_BYTE = 0x00;
            else {
                printf("error 2 \n");
                return -1;
            }

            if (src[i + 1] >= 0x30 && src[i + 1] <= 0x39)
                LOW_HALF_BYTE = src[i + 1] - 0x30;
            else if (src[i + 1] >= 0x41 && src[i + 1] <= 0x46)
                LOW_HALF_BYTE = src[i + 1] - 0x37;
            else if (src[i + 1] >= 0x61 && src[i + 1] <= 0x66)
                LOW_HALF_BYTE = src[i + 1] - 0x57;
            else if (src[i + 1] == 0x20)
                LOW_HALF_BYTE = 0x00;
            else {
                printf("error 3 \n");
                return -1;
            }

            tmp = (HIGH_HALF_BYTE << 4) + LOW_HALF_BYTE;
            out[j] = tmp;
            j++;
        }
    } else {
        // int i;
        for (int i = 0; i < lsrc; i += 3) {
            int tmp = 0;
            int HIGH_HALF_BYTE = 0;
            int LOW_HALF_BYTE = 0;
            if ((i + 2 <= lsrc) && (src[i + 2] != flag)) {
                printf("error 4 \n");
                return 1;
            }

            if (src[i] >= 0x30 && src[i] <= 0x39)
                HIGH_HALF_BYTE = src[i] - 0x30;
            else if (src[i] >= 0x41 && src[i] <= 0x46)
                HIGH_HALF_BYTE = src[i] - 0x37;
            else if (src[i] >= 0x61 && src[i] <= 0x66)
                HIGH_HALF_BYTE = src[i] - 0x57;
            else {
                printf("error 5 \n");
                return -1;
            }

            if (src[i + 1] >= 0x30 && src[i + 1] <= 0x39)
                LOW_HALF_BYTE = src[i + 1] - 0x30;
            else if (src[i + 1] >= 0x41 && src[i + 1] <= 0x46)
                LOW_HALF_BYTE = src[i + 1] - 0x37;
            else if (src[i + 1] >= 0x61 && src[i + 1] <= 0x66)
                LOW_HALF_BYTE = src[i + 1] - 0x57;
            else {
                printf("error 6 \n");
                return -1;
            }

            tmp = (HIGH_HALF_BYTE << 4) + LOW_HALF_BYTE;
            out[j] = tmp;
            j++;
        }
    }

    *lout = j;
    return 0;
}
