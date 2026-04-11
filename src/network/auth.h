#ifndef AUTH_H
#define AUTH_H

#include <Arduino.h>

void initAuth();
bool isDeviceClaimed();
String getApiKey();

// --- TAMBAHAN BARU ---
void refreshToken();
void clearAuth();

#endif