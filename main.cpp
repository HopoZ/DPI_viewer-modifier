#include <windows.h>
#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include <setupapi.h>
#include <hidsdi.h>
#include <winreg.h>
#include <chrono>
#include <thread>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

class DPIDetector {
private:
    static LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
        static DPIDetector* instance = nullptr;
        if (!instance) {
            // 这里需要通过其他方式获取实例
            return CallNextHookEx(NULL, nCode, wParam, lParam);
        }

        if (nCode >= 0 && wParam == WM_MOUSEMOVE) {
            MSLLHOOKSTRUCT* pMouseStruct = (MSLLHOOKSTRUCT*)lParam;
            instance->OnMouseMove(pMouseStruct->pt.x, pMouseStruct->pt.y);
        }
        return CallNextHookEx(NULL, nCode, wParam, lParam);
    }

    struct MouseMovement {
        int deltaX, deltaY;
        DWORD timestamp;
    };

    std::vector<MouseMovement> movements;
    bool isRecording = false;
    HHOOK mouseHook = nullptr;
    POINT lastPos = {0, 0};

public:
    // 方法1: 获取基本鼠标信息
    void GetBasicMouseInfo() {
        std::cout << "\n=== 方法1: 基本鼠标信息 ===" << std::endl;

        // 获取鼠标速度
        int speed = 0;
        SystemParametersInfo(SPI_GETMOUSESPEED, 0, &speed, 0);
        std::cout << "系统鼠标速度: " << speed << "/20" << std::endl;

        // 获取鼠标加速
        int mouseParams[3];
        SystemParametersInfo(SPI_GETMOUSE, 0, mouseParams, 0);
        std::cout << "鼠标加速设置: [" << mouseParams[0] << ", " << mouseParams[1] << ", " << mouseParams[2] << "]" << std::endl;

        // 获取原始输入设备信息
        UINT nDevices;
        GetRawInputDeviceList(nullptr, &nDevices, sizeof(RAWINPUTDEVICELIST));

        RAWINPUTDEVICELIST *pRawInputDeviceList = new RAWINPUTDEVICELIST[nDevices];
        GetRawInputDeviceList(pRawInputDeviceList, &nDevices, sizeof(RAWINPUTDEVICELIST));

        for (UINT i = 0; i < nDevices; i++) {
            if (pRawInputDeviceList[i].dwType == RIM_TYPEMOUSE) {
                RID_DEVICE_INFO rdi;
                UINT cbSize = sizeof(RID_DEVICE_INFO);
                GetRawInputDeviceInfo(pRawInputDeviceList[i].hDevice, RIDI_DEVICEINFO, &rdi, &cbSize);

                std::cout << "\n鼠标设备 #" << i << ":" << std::endl;
                std::cout << "  按键数: " << rdi.mouse.dwNumberOfButtons << std::endl;
                std::cout << "  采样率: " << rdi.mouse.dwSampleRate << " Hz" << std::endl;
                std::cout << "  是否有滚轮: " << (rdi.mouse.fHasHorizontalWheel ? "是" : "否") << std::endl;

                // 获取设备名称
                UINT nameSize = 0;
                GetRawInputDeviceInfo(pRawInputDeviceList[i].hDevice, RIDI_DEVICENAME, nullptr, &nameSize);
                if (nameSize > 0) {
                    wchar_t* deviceName = new wchar_t[nameSize];
                    GetRawInputDeviceInfo(pRawInputDeviceList[i].hDevice, RIDI_DEVICENAME, deviceName, &nameSize);
                    std::wcout << L"  设备名: " << deviceName << std::endl;
                    delete[] deviceName;
                }
            }
        }
        delete[] pRawInputDeviceList;
    }

    // 方法2: 通过注册表查找鼠标信息
    void GetMouseInfoFromRegistry() {
        std::cout << "\n=== 方法2: 注册表鼠标信息 ===" << std::endl;

        HKEY hKey;
        const wchar_t* subKey = L"SYSTEM\\CurrentControlSet\\Enum\\HID";

        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            DWORD index = 0;
            wchar_t keyName[256];
            DWORD keyNameSize = sizeof(keyName) / sizeof(wchar_t);

            while (RegEnumKeyExW(hKey, index++, keyName, &keyNameSize, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
                if (wcsstr(keyName, L"VID_") && wcsstr(keyName, L"PID_")) {
                    std::wcout << L"找到HID设备: " << keyName << std::endl;

                    // 尝试打开子键获取更多信息
                    HKEY hSubKey;
                    if (RegOpenKeyExW(hKey, keyName, 0, KEY_READ, &hSubKey) == ERROR_SUCCESS) {
                        // 枚举实例
                        DWORD subIndex = 0;
                        wchar_t subKeyName[256];
                        DWORD subKeyNameSize = sizeof(subKeyName) / sizeof(wchar_t);

                        while (RegEnumKeyExW(hSubKey, subIndex++, subKeyName, &subKeyNameSize, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
                            HKEY hInstanceKey;
                            if (RegOpenKeyExW(hSubKey, subKeyName, 0, KEY_READ, &hInstanceKey) == ERROR_SUCCESS) {
                                // 读取设备描述
                                wchar_t deviceDesc[256];
                                DWORD descSize = sizeof(deviceDesc);
                                if (RegQueryValueExW(hInstanceKey, L"DeviceDesc", NULL, NULL, (LPBYTE)deviceDesc, &descSize) == ERROR_SUCCESS) {
                                    std::wcout << L"  设备描述: " << deviceDesc << std::endl;
                                }

                                // 读取制造商
                                wchar_t mfg[256];
                                DWORD mfgSize = sizeof(mfg);
                                if (RegQueryValueExW(hInstanceKey, L"Mfg", NULL, NULL, (LPBYTE)mfg, &mfgSize) == ERROR_SUCCESS) {
                                    std::wcout << L"  制造商: " << mfg << std::endl;
                                }

                                RegCloseKey(hInstanceKey);
                            }
                            subKeyNameSize = sizeof(subKeyName) / sizeof(wchar_t);
                        }
                        RegCloseKey(hSubKey);
                    }
                }
                keyNameSize = sizeof(keyName) / sizeof(wchar_t);
            }
            RegCloseKey(hKey);
        } else {
            std::cout << "无法访问注册表鼠标信息" << std::endl;
        }
    }

    // 方法3: 使用HID API获取详细信息 (暂时禁用，需要特殊的HID库)
    void GetMouseInfoFromHID() {
        std::cout << "\n=== 方法3: HID API 鼠标信息 ===" << std::endl;
        std::cout << "此功能需要特殊的HID库支持，当前版本暂时禁用" << std::endl;
        std::cout << "可以尝试使用其他方法获取鼠标信息" << std::endl;
    }

    // 方法4: 通过测量鼠标移动估算DPI
    void EstimateDPIByMovement() {
        std::cout << "\n=== 方法4: 通过移动估算DPI ===" << std::endl;
        std::cout << "请在接下来的10秒内，将鼠标从屏幕左边缘移动到右边缘..." << std::endl;
        std::cout << "3秒后开始..." << std::endl;

        std::this_thread::sleep_for(std::chrono::seconds(3));
        std::cout << "开始测量！请移动鼠标..." << std::endl;

        // 获取屏幕分辨率
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        std::cout << "屏幕分辨率: " << screenWidth << "x" << screenHeight << std::endl;

        POINT startPos, endPos;
        GetCursorPos(&startPos);

        auto startTime = std::chrono::high_resolution_clock::now();
        std::this_thread::sleep_for(std::chrono::seconds(10));
        auto endTime = std::chrono::high_resolution_clock::now();

        GetCursorPos(&endPos);

        int pixelsMoved = abs(endPos.x - startPos.x);
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

        std::cout << "测量结果:" << std::endl;
        std::cout << "  起始位置: (" << startPos.x << ", " << startPos.y << ")" << std::endl;
        std::cout << "  结束位置: (" << endPos.x << ", " << endPos.y << ")" << std::endl;
        std::cout << "  移动像素: " << pixelsMoved << " pixels" << std::endl;
        std::cout << "  测量时间: " << duration.count() << " ms" << std::endl;

        if (pixelsMoved > 100) {
            // 假设用户移动了大约1英寸的物理距离
            double estimatedDPI = pixelsMoved / 1.0; // 粗略估算
            std::cout << "  估算DPI: ~" << (int)estimatedDPI << " (假设移动了1英寸)" << std::endl;
            std::cout << "  注意: 这只是粗略估算，实际DPI可能不同" << std::endl;
        } else {
            std::cout << "  移动距离太短，无法准确估算DPI" << std::endl;
        }
    }

    void OnMouseMove(int x, int y) {
        if (!isRecording) return;

        MouseMovement movement;
        movement.deltaX = x - lastPos.x;
        movement.deltaY = y - lastPos.y;
        movement.timestamp = GetTickCount();

        if (abs(movement.deltaX) > 0 || abs(movement.deltaY) > 0) {
            movements.push_back(movement);
        }

        lastPos.x = x;
        lastPos.y = y;
    }

    // 方法5: 显示系统DPI设置
    void GetSystemDPIInfo() {
        std::cout << "\n=== 方法5: 系统DPI信息 ===" << std::endl;

        HDC hdc = GetDC(NULL);
        int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
        int dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
        ReleaseDC(NULL, hdc);

        std::cout << "系统DPI设置:" << std::endl;
        std::cout << "  水平DPI: " << dpiX << std::endl;
        std::cout << "  垂直DPI: " << dpiY << std::endl;

        // 获取DPI缩放比例
        double scaleX = dpiX / 96.0;
        double scaleY = dpiY / 96.0;
        std::cout << "  缩放比例: " << std::fixed << std::setprecision(2) << scaleX * 100 << "%" << std::endl;

        // 注意：这是显示器DPI，不是鼠标DPI
        std::cout << "  注意: 这是显示器DPI，不是鼠标DPI" << std::endl;
    }
};

void ShowMenu() {
    std::cout << "\n==================== DPI 检测工具 ====================" << std::endl;
    std::cout << "1. 获取基本鼠标信息" << std::endl;
    std::cout << "2. 从注册表读取鼠标信息" << std::endl;
    std::cout << "3. 使用HID API获取详细信息" << std::endl;
    std::cout << "4. 通过移动估算DPI" << std::endl;
    std::cout << "5. 显示系统DPI信息" << std::endl;
    std::cout << "6. 运行所有检测方法" << std::endl;
    std::cout << "0. 退出" << std::endl;
    std::cout << "=====================================================" << std::endl;
    std::cout << "请选择 (0-6): ";
}

int main() {
    // 设置控制台支持Unicode
    SetConsoleOutputCP(CP_UTF8);

    DPIDetector detector;
    int choice;

    std::cout << "DPI检测工具 - 多种方法检测鼠标DPI" << std::endl;
    std::cout << "注意: Windows没有标准API直接获取鼠标DPI，以下方法提供不同的检测途径" << std::endl;

    do {
        ShowMenu();
        std::cin >> choice;

        switch (choice) {
            case 1:
                detector.GetBasicMouseInfo();
                break;
            case 2:
                detector.GetMouseInfoFromRegistry();
                break;
            case 3:
                detector.GetMouseInfoFromHID();
                break;
            case 4:
                detector.EstimateDPIByMovement();
                break;
            case 5:
                detector.GetSystemDPIInfo();
                break;
            case 6:
                std::cout << "\n运行所有检测方法..." << std::endl;
                detector.GetBasicMouseInfo();
                detector.GetMouseInfoFromRegistry();
                detector.GetMouseInfoFromHID();
                detector.GetSystemDPIInfo();
                std::cout << "\n是否要进行移动测试? (y/n): ";
                char test;
                std::cin >> test;
                if (test == 'y' || test == 'Y') {
                    detector.EstimateDPIByMovement();
                }
                break;
            case 0:
                std::cout << "退出程序..." << std::endl;
                break;
            default:
                std::cout << "无效选择，请重新输入" << std::endl;
                break;
        }

        if (choice != 0) {
            std::cout << "\n按回车键继续...";
            std::cin.ignore();
            std::cin.get();
        }

    } while (choice != 0);

    return 0;
}
