/* Copyright (c) 2017, William Breathitt Gray
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY
 * WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

struct event_input{
        INPUT ei;
        struct event_input *next;
        unsigned long delay_time;
};

unsigned first_event = 1;
unsigned long first_event_delay;
unsigned long prev_event_time;
unsigned loop = 1;

FILE *record_fp = NULL;

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam){
        if(nCode == HC_ACTION){
                KBDLLHOOKSTRUCT *kbinfo = lParam;

                unsigned long flags = 0;
                if(LLKHF_EXTENDED & kbinfo->flags){
                        flags |= KEYEVENTF_EXTENDEDKEY;
                }
                if(LLKHF_UP & kbinfo->flags){
                        flags |= KEYEVENTF_KEYUP;
                }
                unsigned long event_time;
                if(first_event){
                        event_time = first_event_delay;
                        first_event = 0;
                }else{
                        event_time = kbinfo->time - prev_event_time;
                }

                fprintf(record_fp, "1 0x%lX 0x%lX 0x%lX 0x%lX 0x%lX\n", (unsigned long)kbinfo->vkCode, (unsigned long)kbinfo->scanCode, flags, event_time, (unsigned long)kbinfo->dwExtraInfo);

                prev_event_time = kbinfo->time;
        }

        return CallNextHookEx(NULL, nCode, wParam, lParam);
}

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam){
        if(nCode == HC_ACTION){
                MSLLHOOKSTRUCT *mouseinfo = lParam;

                int x = mouseinfo->pt.x;
                int y = mouseinfo->pt.y;
                RECT desktop;
                if(GetWindowRect(GetDesktopWindow(), &desktop)){
                        x = 65535 * (double)x/desktop.right;
                        y = 65535 * (double)y/desktop.bottom;
                }
                unsigned long flags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
                switch(wParam){
                        case WM_LBUTTONDOWN:
                                flags |= MOUSEEVENTF_LEFTDOWN;
                                break;
                        case WM_LBUTTONUP:
                                flags |= MOUSEEVENTF_LEFTUP;
                                break;
                        case WM_MOUSEMOVE:
                                flags |= MOUSEEVENTF_MOVE;
                                break;
                        case WM_RBUTTONDOWN:
                                flags |= MOUSEEVENTF_RIGHTDOWN;
                                break;
                        case WM_RBUTTONUP:
                                flags |= MOUSEEVENTF_RIGHTUP;
                                break;
                }
                unsigned long event_time;
                if(first_event){
                        event_time = first_event_delay;
                        first_event = 0;
                }else{
                        event_time = mouseinfo->time - prev_event_time;
                }

                fprintf(record_fp, "0 %d %d 0x%lX 0x%lX 0x%lX 0x%lX\n", x, y, (unsigned long)mouseinfo->mouseData, flags, event_time, (unsigned long)mouseinfo->dwExtraInfo);
 
                prev_event_time = mouseinfo->time;
        }

        return CallNextHookEx(NULL, nCode, wParam, lParam);
}

void freeEvents(struct event_input *head){
        while(head){
                struct event_input *curr = head;
                head = head->next;
                free(curr);
        }
}

unsigned playEvents(FILE *fp){
        char buffer[256];
        struct event_input *head = NULL;
        struct event_input *end = NULL;
        while(fgets(buffer, sizeof(buffer), fp)){
                struct event_input *curr = calloc(1, sizeof(*curr));
                if(!curr){
                        fprintf(stderr, "Unable to allocate memory for event\n");
                        goto playEvents_err;
                }
                if(!end){
                        head = curr;
                }else{
                        end->next = curr;
                }
                end = curr;

                char *event_info;
                const unsigned event_type = strtoul(buffer, &event_info, 0);
                if(event_type){
                        unsigned long wVk;
                        unsigned long wScan;
                        unsigned long dwFlags;
                        unsigned long dwExtraInfo;
                        const int retval = sscanf(event_info, "%lX %lX %lX %lX %lX", &wVk, &wScan, &dwFlags, &curr->delay_time, &dwExtraInfo);
                        if(retval < 5){
                                fprintf(stderr, "Recording file syntax error\n");
                                goto playEvents_err;
                        }
                        curr->ei.type = INPUT_KEYBOARD;
                        curr->ei.ki.wVk = wVk;
                        curr->ei.ki.wScan = wScan;
                        curr->ei.ki.dwFlags = dwFlags;
                        curr->ei.ki.dwExtraInfo = dwExtraInfo;
                }else{
                        unsigned long mouseData;
                        unsigned long dwFlags;
                        unsigned long dwExtraInfo;
                        const int retval = sscanf(event_info, "%d %d %lX %lX %lX %lX", &curr->ei.mi.dx, &curr->ei.mi.dy, &mouseData, &dwFlags, &curr->delay_time, &dwExtraInfo);
                        if(retval < 6){
                                fprintf(stderr, "Recording file syntax error\n");
                                goto playEvents_err;
                        }
                        curr->ei.type = INPUT_MOUSE;
                        curr->ei.mi.mouseData = mouseData;
                        curr->ei.mi.dwFlags = dwFlags;
                        curr->ei.mi.dwExtraInfo = dwExtraInfo;
                }
        }
        if(ferror(fp)){
                fprintf(stderr, "Recording file read error\n");
                goto playEvents_err;
        }

        while(loop--){
                struct event_input *curr = head;
                while(curr){
                        Sleep(curr->delay_time);
                        if(!SendInput(1, &curr->ei, sizeof(curr->ei))){
                                fprintf(stderr, "Event input insertion failure\n");
                                goto playEvents_err;
                        }
                        curr = curr->next;
                }
        }

        freeEvents(head);

        return 0;

playEvents_err:
        freeEvents(head);
        return 1;
}

unsigned recordEvents(const unsigned long delay_record_time, const unsigned long record_time){
        HHOOK hhkLowLevelKybd = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);
        if(!hhkLowLevelKybd){
                fprintf(stderr, "ERROR: Unable to attach low-level keyboard hook\n");
                return 1;
        }
        HHOOK hhkLowLevelMouse = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, NULL, 0);
        if(!hhkLowLevelMouse){
                fprintf(stderr, "ERROR: Unable to attach low-level mouse hook\n");
                goto err_hhkLowLevelMouse;
        }

        Sleep(delay_record_time);

        const DWORD start_tick_count = GetTickCount();
        do{
                MSG msg;
                if(PeekMessage(&msg, NULL, 0, 0, 0)){
                        fprintf(stderr, "ERROR: Unable to peek message\n");
                        goto err_peekmessage;
                }
                TranslateMessage(&msg);
                DispatchMessage(&msg);
        }while(GetTickCount() - start_tick_count < record_time);

        UnhookWindowsHookEx(hhkLowLevelMouse);
        UnhookWindowsHookEx(hhkLowLevelKybd);

        return 0;

err_peekmessage:
        UnhookWindowsHookEx(hhkLowLevelMouse);
err_hhkLowLevelMouse:
        UnhookWindowsHookEx(hhkLowLevelKybd);
        return 1;
}

int main(void){
        unsigned choice;
        do{
                printf("Enter 0 to record or 1 to play:\n");
                char buffer[32] = "";
                if(!fgets(buffer, sizeof(buffer), stdin)){
                        continue;
                }
                choice = strtoul(buffer, NULL, 0);
        }while(choice > 1);

        if(choice){
                FILE *fp = NULL;
                do{
                        printf("Enter path to recording file:\n");
                        char path[256] = "";
                        if(!fgets(path, sizeof(path), stdin)){
                                continue;
                        }
                        path[255] = '\0';
                        size_t cindex = 254;
                        while(cindex--){
                                if(isalnum(path[cindex])){
                                        path[cindex+1] = '\0';
                                        break;
                                }
                        };
                        fp = fopen(path, "r");
                        if(!fp){
                                fprintf(stderr, "Error opening %s\n", path);
                        }else{
                                break;
                        }
                }while(1);
                do{
                        printf("Enter number of times to loop:\n");
                        char buffer[32] = "";
                        if(!fgets(buffer, sizeof(buffer), stdin)){
                                continue;
                        }
                        loop = strtoul(buffer, NULL, 0);
                }while(0);
                if(playEvents(fp)){
                        fclose(fp);
                        return 1;
                }
                fclose(fp);
                printf("Playback complete!\n");
        }else{
                do{
                        printf("Enter path to recording file:\n");
                        char path[256] = "";
                        if(!fgets(path, sizeof(path), stdin)){
                                continue;
                        }
                        path[255] = '\0';
                        size_t cindex = 254;
                        while(cindex--){
                                if(isalnum(path[cindex])){
                                        path[cindex+1] = '\0';
                                        break;
                                }
                        };
                        record_fp = fopen(path, "w");
                        if(!record_fp){
                                fprintf(stderr, "Error opening %s\n", path);
                        }else{
                                break;
                        }
                }while(1);
                unsigned long delay_record_time;
                do{
                        printf("Enter the time in milliseconds to wait before recording:\n");
                        char buffer[32] = "";
                        if(!fgets(buffer, sizeof(buffer), stdin)){
                                continue;
                        }
                        delay_record_time = strtoul(buffer, NULL, 0);
                }while(0);
                unsigned long record_time;
                do{
                        printf("Enter the time in milliseconds for the recording length:\n");
                        char buffer[32] = "";
                        if(!fgets(buffer, sizeof(buffer), stdin)){
                                continue;
                        }
                        record_time = strtoul(buffer, NULL, 0);
                }while(0);
                do{
                        printf("Enter the time in milliseconds for the first event's delay value:\n");
                        char buffer[32] = "";
                        if(!fgets(buffer, sizeof(buffer), stdin)){
                                continue;
                        }
                        first_event_delay = strtoul(buffer, NULL, 0);
                }while(0);
                if(recordEvents(delay_record_time, record_time)){
                        fclose(record_fp);
                        return 1;
                }
                fclose(record_fp);
                printf("Recording complete!\n");
        }
        return 0;
}
