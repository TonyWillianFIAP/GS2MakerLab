// =====================================================
// GUIDING HAND - Configuracao do dispositivo
// =====================================================
// Credenciais e enderecos ficam fora do codigo principal.
// No Wokwi, use a rede publica "Wokwi-GUEST" (sem senha, canal 6).
// No dispositivo real, preencha com os dados reais e NAO envie
// este arquivo com senhas para o GitHub (adicione-o ao .gitignore).
// =====================================================

#ifndef CONFIG_H
#define CONFIG_H

// Rede Wi-Fi (celular guardado no bolso ou roteador 4G de bolso)
#define WIFI_SSID   "Wokwi-GUEST"
#define WIFI_SENHA  ""
#define WIFI_CANAL  6          // No hardware real, use 0 (automatico)

// Endereco da API do Guiding Hand.
// Deixe vazio ("") para simular: os comandos do servidor de visao
// sao digitados no Monitor Serial e os eventos aparecem no Serial.
#define SERVIDOR_URL ""        // Ex.: "http://api.exemplo.com.br"

// Identificacao do dispositivo (nenhum dado pessoal do usuario)
#define DISPOSITIVO_ID    "gh-001"
#define DISPOSITIVO_TOKEN "token-de-teste"

#endif
