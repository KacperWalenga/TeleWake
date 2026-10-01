#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <UniversalTelegramBot.h>

#include "secrets.h"

constexpr uint16_t WOL_PORT = 9;
constexpr uint16_t LOCAL_UDP_PORT = 40000;

constexpr unsigned long TELEGRAM_POLL_INTERVAL_MS = 2000;
constexpr unsigned long WIFI_RECONNECT_INTERVAL_MS = 10000;

const uint8_t PC_MAC[6] = PC_MAC_BYTES;

WiFiClientSecure secureClient;
UniversalTelegramBot bot(BOT_TOKEN, secureClient);
WiFiUDP udp;

unsigned long lastTelegramPoll = 0;
unsigned long lastWiFiReconnectAttempt = 0;

IPAddress getBroadcastAddress()
{
    IPAddress localIp = WiFi.localIP();
    IPAddress subnetMask = WiFi.subnetMask();
    IPAddress broadcast;

    for (int i = 0; i < 4; i++)
    {
        broadcast[i] = localIp[i] | ~subnetMask[i];
    }

    return broadcast;
}

void connectToWiFi()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        return;
    }

    Serial.print("Connecting to Wi-Fi: ");
    Serial.println(WIFI_SSID);

    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);

    WiFi.disconnect();
    delay(1000);

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long startedAt = millis();

    while (
        WiFi.status() != WL_CONNECTED &&
        millis() - startedAt < 30000
    )
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println();

    wl_status_t status = WiFi.status();

    Serial.print("Wi-Fi status code: ");
    Serial.println(static_cast<int>(status));

    if (status == WL_CONNECTED)
    {
        Serial.println("Wi-Fi connected.");

        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());

        Serial.print("Subnet mask: ");
        Serial.println(WiFi.subnetMask());

        Serial.print("Gateway: ");
        Serial.println(WiFi.gatewayIP());

        Serial.print("Broadcast: ");
        Serial.println(getBroadcastAddress());
    }
    else
    {
        Serial.println("Wi-Fi connection failed.");
    }
}

bool sendWakeOnLan()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println(
            "Cannot send WoL packet: Wi-Fi is disconnected."
        );

        return false;
    }

    constexpr size_t MAGIC_PACKET_SIZE = 102;
    uint8_t magicPacket[MAGIC_PACKET_SIZE];

    memset(magicPacket, 0xFF, 6);

    for (int repetition = 0; repetition < 16; repetition++)
    {
        memcpy(
            &magicPacket[6 + repetition * 6],
            PC_MAC,
            6
        );
    }

    IPAddress broadcastAddress = getBroadcastAddress();

    Serial.print("Sending Wake-on-LAN packet to ");
    Serial.print(broadcastAddress);
    Serial.print(":");
    Serial.println(WOL_PORT);

    bool success = true;

    for (int attempt = 0; attempt < 3; attempt++)
    {
        if (!udp.beginPacket(broadcastAddress, WOL_PORT))
        {
            Serial.println("Failed to start UDP packet.");
            success = false;
            continue;
        }

        size_t bytesWritten = udp.write(
            magicPacket,
            sizeof(magicPacket)
        );

        if (bytesWritten != sizeof(magicPacket))
        {
            Serial.println(
                "Failed to write complete WoL packet."
            );

            success = false;
        }

        if (!udp.endPacket())
        {
            Serial.println("Failed to send UDP packet.");
            success = false;
        }

        delay(150);
    }

    return success;
}

void handleTelegramMessages(int messageCount)
{
    for (int i = 0; i < messageCount; i++)
    {
        String chatId = bot.messages[i].chat_id;
        String text = bot.messages[i].text;
        String senderName = bot.messages[i].from_name;

        text.trim();
        text.toLowerCase();

        Serial.print("Message from ");
        Serial.print(senderName);
        Serial.print(" [");
        Serial.print(chatId);
        Serial.print("]: ");
        Serial.println(text);

        if (chatId != ALLOWED_CHAT_ID)
        {
            Serial.println("Unauthorized user rejected.");

            bot.sendMessage(
                chatId,
                "You are not authorized to use this bot.",
                ""
            );

            continue;
        }

        if (text == "/start" || text == "/help")
        {
            String response;

            response += "TeleWake is online.\n\n";
            response += "/wake - power on the computer\n";
            response += "/status - show ESP32 status\n";
            response += "/help - show available commands";

            bot.sendMessage(chatId, response, "");
        }
        else if (text == "/wake")
        {
            bot.sendMessage(
                chatId,
                "Sending Wake-on-LAN packet...",
                ""
            );

            bool success = sendWakeOnLan();

            if (success)
            {
                bot.sendMessage(
                    chatId,
                    "Wake-on-LAN packet sent.",
                    ""
                );
            }
            else
            {
                bot.sendMessage(
                    chatId,
                    "Failed to send Wake-on-LAN packet.",
                    ""
                );
            }
        }
        else if (text == "/status")
        {
            String response;

            response += "ESP32 is online.\n";

            response += "IP: ";
            response += WiFi.localIP().toString();

            response += "\nBroadcast: ";
            response += getBroadcastAddress().toString();

            response += "\nRSSI: ";
            response += String(WiFi.RSSI());
            response += " dBm";

            response += "\nUptime: ";
            response += String(millis() / 1000);
            response += " s";

            bot.sendMessage(chatId, response, "");
        }
        else
        {
            bot.sendMessage(
                chatId,
                "Unknown command. Use /help.",
                ""
            );
        }
    }
}

void checkTelegramMessages()
{
    int messageCount = bot.getUpdates(
        bot.last_message_received + 1
    );

    while (messageCount > 0)
    {
        Serial.printf(
            "Received %d message(s).\n",
            messageCount
        );

        handleTelegramMessages(messageCount);

        messageCount = bot.getUpdates(
            bot.last_message_received + 1
        );
    }
}

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("==============================");
    Serial.println("TeleWake ESP32");
    Serial.println("==============================");

    connectToWiFi();

    secureClient.setInsecure();

    if (udp.begin(LOCAL_UDP_PORT))
    {
        Serial.println("UDP initialized.");
    }
    else
    {
        Serial.println("Failed to initialize UDP.");
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        bot.sendMessage(
            ALLOWED_CHAT_ID,
            "TeleWake ESP32 started successfully.",
            ""
        );
    }
}

void loop()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        if (
            millis() - lastWiFiReconnectAttempt >=
            WIFI_RECONNECT_INTERVAL_MS
        )
        {
            lastWiFiReconnectAttempt = millis();
            connectToWiFi();
        }

        delay(10);
        return;
    }

    if (
        millis() - lastTelegramPoll >=
        TELEGRAM_POLL_INTERVAL_MS
    )
    {
        lastTelegramPoll = millis();
        checkTelegramMessages();
    }

    delay(10);
}