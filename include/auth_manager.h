#ifndef AUTH_MANAGER_H
#define AUTH_MANAGER_H

#include <Arduino.h>

void initAuth();
bool isDeviceClaimed();
String getApiKey();

// --- TAMBAHAN BARU ---
void refreshToken();
void clearAuth();

#endif