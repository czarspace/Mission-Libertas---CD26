#ifndef MODE_MANAGER_H
#define MODE_MANAGER_H
#pragma once

#include "Config.h"

// Interpreta um pacote recebido (comando da GS ou repasse) e decide o que transmitir
void processarPacote(const PacoteRecebido* p);

#endif
