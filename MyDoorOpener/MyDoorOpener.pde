//----------------------------------------------------------------------------------------------------
// Copyright (C) 2009 - 2010, MyDoorOpener.com
//----------------------------------------------------------------------------------------------------
//
// Release Notes:
//
// v1.0 [02/23/2010] - Initial release
// v1.1 [03/02/2010] - Added support for up to three garage doors.
// v1.2 [08/11/2010] - Deprecated HTTP POST in favor of HTTP GET parameters.
//                   - Added additional serial debugging.
// v1.3 [06/04/2011] - Support for Arduino Uno as well as latest Arduino IDE (v0022).
//
//----------------------------------------------------------------------------------------------------

// Uncomment to turn ON serial debugging

// #define MYDOOROPENER_SERIAL_DEBUGGING 1
// #define WEBDUINO_SERIAL_DEBUGGING 2

// the SPI library include is required in order to compile using the v0022 Arduino IDE. If
// using v0017 Arduino IDE, remove or comment the SPI library include statement.
#include <SPI.h>

// standard includes required for MyDoorOpener compilation

#include "Ethernet.h"
#include "WebServer.h"
#include "Time.h"
#include "aes256.h"

//*******************************************************************
//*******************************************************************

// EthernetShield MAC address.

static uint8_t mac[6] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

// EthernetShield IP address (DHCP reserved, never allocated to anyone else).

static uint8_t ip[4] = { 192, 168, 0, 13 };

// password required for operating door [max length = 16] (status fetching doesn't require password)

#define PASSWORD "Cyber111222"

//*******************************************************************
//*******************************************************************

// Arduino HTTP server listening port number.

WebServer webserver("", 80);

// minimum analog value (0-1023) to consider door status == opened,
// anything below this pseudo-voltage value will be considered status == closed.

#define STATUS_OPEN_TRESHOLD 1000

// value to set for STATUS_PIN when feature is disabled.

#define STATUS_FEEDBACK_DISABLED -1

// value to set for RELAY_PIN when feature is disabled.

#define RELAY_DISABLED -1

// status contact should be connected to this analog input pin (anologRead).
// Set to STATUS_FEEDBACK_DISABLED to disable status feedback feature.

#define DOOR1_STATUS_PIN 3
#define DOOR2_STATUS_PIN STATUS_FEEDBACK_DISABLED
#define DOOR3_STATUS_PIN STATUS_FEEDBACK_DISABLED

// open/close trigger relay should be connected to this digital output pin (digitalWrite).

#define DOOR1_RELAY_PIN 9
#define DOOR2_RELAY_PIN RELAY_DISABLED
#define DOOR3_RELAY_PIN RELAY_DISABLED

// number of milliseconds RELAY_PIN will be held high when triggered.

#define RELAY_DELAY 1000

// misc size constants

#define HTTP_PARAM_NAME_SIZE 16
#define HTTP_PARAM_VALUE_SIZE 64

#define PASSWORD_HEX_SIZE 32
#define PASSWORD_SIZE 16
#define AES256_CRYPTO_KEY_SIZE 32
#define CHALLENGE_TOKEN_SIZE 16

//----------------------------------------------------------------------------------------------------
void output(WebServer &server, char* data, bool newLine)
{
  #ifdef MYDOOROPENER_SERIAL_DEBUGGING
    if (newLine)
      Serial.println(data);
    else
      Serial.print(data);
  #endif

  if (newLine)
    server.println(data);
  else
    server.print(data);
}

//----------------------------------------------------------------------------------------------------
void output(WebServer &server, int number, bool newLine)
{
  char str[10] = "";
  itoa(number, str, 10);

  output(server, str, newLine);
}

//----------------------------------------------------------------------------------------------------
void webRequestHandler(WebServer &server, WebServer::ConnectionType type, char *url, bool isUrlComplete)
{
  #ifdef MYDOOROPENER_SERIAL_DEBUGGING
    Serial.print("*** Request URL: ");
    Serial.print(url ? url : "<NULL>");
    Serial.println(" ***");
  #endif

  // holder for submitted password (as hex)

  char submitPassword[PASSWORD_HEX_SIZE + 1];
  memset(&submitPassword, 0, sizeof(submitPassword));

  // door on which open/close action is to be carried out. If unspecified, assume door #1

  int relayPin = DOOR1_RELAY_PIN;

  // holder for current challenge token value. The following must not be
  // initialized (memset) because it is static and must persist across HTTP calls

  static char currentChallengeToken[CHALLENGE_TOKEN_SIZE + 1] = "";

  // handle HTTP GET params (if provided)

  char name[HTTP_PARAM_NAME_SIZE + 1];
  char value[HTTP_PARAM_VALUE_SIZE + 1];

  // process all HTTP GET parameters

  if (type == WebServer::GET)
  {
    #ifdef MYDOOROPENER_SERIAL_DEBUGGING
      Serial.println("*** GET Request ***");
    #endif

    while (url && strlen(url))
    {
      // process each HTTP GET parameter, one at a time

      memset(&name, 0, sizeof(name));
      memset(&value, 0, sizeof(value));

      server.nextURLparam(&url, name, HTTP_PARAM_NAME_SIZE, value, HTTP_PARAM_VALUE_SIZE);

      #ifdef MYDOOROPENER_SERIAL_DEBUGGING
        Serial.print("*** HTTP GET PARAM - name: '");
        Serial.print(name);
        Serial.print("' - ");
        Serial.print("value: '");
        Serial.print(value);
        Serial.println("' ***");
      #endif

      // keep hold of submitted encrypted hex password value

      if (strcmp(name, "password") == 0)
        strcpy(submitPassword, value);

      // keep hold of relay pin which should be triggered

      else if (strcmp(name, "doorNum") == 0)
      {
        if (value[0] == '1')
            relayPin = DOOR1_RELAY_PIN;
         else if (value[0] == '2')
            relayPin = DOOR2_RELAY_PIN;
         else if (value[0] == '3')
            relayPin = DOOR3_RELAY_PIN;
         else
            relayPin = DOOR1_RELAY_PIN; // default when doorNum value is out of range
      }
    }
  }

  // the presence of an HTTP GET password param results in a request
  // to trigger the relay (used to be triggered by an HTTP request of type POST)

  if(strlen(submitPassword) > 0)
  {
    #ifdef MYDOOROPENER_SERIAL_DEBUGGING
      Serial.print("*** submitPassword: '");
      Serial.print(submitPassword);
      Serial.println("' ***");
    #endif

    // decrypt password using latest challenge token as cypher key

    uint8_t cryptoKey[AES256_CRYPTO_KEY_SIZE + 1];
    memset(&cryptoKey, 0, sizeof(cryptoKey));

    for (int i = 0; i < strlen(currentChallengeToken); ++i)
      cryptoKey[i] = currentChallengeToken[i];

    uint8_t password[PASSWORD_SIZE + 1];
    memset(&password, 0, sizeof(password));

    // convert password from hex string to ascii decimal

    int i = 0;
    int j = 0;
    while (true)
    {
      if (!submitPassword[j])
        break;

      char hexValue[3] = { submitPassword[j], submitPassword[j+1], '\0' };
      password[i] = (int) strtol(hexValue, NULL, 16);

      i += 1;
      j += 2;
    }

    // proceed with AES256 password decryption

    aes256_context ctx;
    aes256_init(&ctx, cryptoKey);
    aes256_decrypt_ecb(&ctx, password);
    aes256_done(&ctx);

    char passwordAsChar[PASSWORD_SIZE + 1];
    memset(&passwordAsChar, 0, sizeof(passwordAsChar));

    for (int i = 0; i < sizeof(password); ++i)
      passwordAsChar[i] = password[i];

    #ifdef MYDOOROPENER_SERIAL_DEBUGGING
      Serial.print("*** passwordAsChar: '");
      Serial.print(passwordAsChar);
      Serial.println("' ***");
    #endif

    // if password matches, trigger relay

    if (strcmp(passwordAsChar, PASSWORD) == 0)
    {
      #ifdef MYDOOROPENER_SERIAL_DEBUGGING
        Serial.println("*** relay triggered ***");
      #endif

      // trigger RELAY_PIN and hold it HIGH for the appropriate number of milliseconds

      digitalWrite(relayPin, HIGH);
      delay(RELAY_DELAY);
      digitalWrite(relayPin, LOW);
    }
  }

  // write HTTP headers

  server.httpSuccess("text/xml; charset=utf-8");

  #ifdef MYDOOROPENER_SERIAL_DEBUGGING
    Serial.println("*** XML output begin ***");
  #endif

  // write opening XML element to output stream

  output(server, "<?xml version=\"1.0\"?>", true);
  output(server, "<myDoorOpener>", true);

  // write current door status

  int doorStatus[][2] =
  {
    { DOOR1_STATUS_PIN, 1 },
    { DOOR2_STATUS_PIN, 2 },
    { DOOR3_STATUS_PIN, 3 }
  };

  for (int i = 0; i < 3; ++i)
  {
    output(server, "<status doorNum=\"", false);
    output(server, doorStatus[i][1], false);
    output(server, "\">", false);

    // make sure status feedback feature is enabled for given door

    if (doorStatus[i][0] != STATUS_FEEDBACK_DISABLED)
    {
      // read current open/close state from STATUS_PIN

      int status = analogRead(doorStatus[i][0]);

      // write current open/close state to output stream

      output(server, (char*)((status >= STATUS_OPEN_TRESHOLD) ? "Opened" : "Closed"), false);
    }
    else
      output(server, "Unknown", false);

    output(server, "</status>", true);
  }

  // re-generate new challenge token

  sprintf(currentChallengeToken, "Cyber%i%i%i", hour(), minute(), second());

  // write challenge token to output stream

  output(server, "<challengeToken>", false);
  output(server, currentChallengeToken, false);
  output(server, "</challengeToken>", true);

  // write closing XML element to output stream

  output(server, "</myDoorOpener>", true);

  #ifdef MYDOOROPENER_SERIAL_DEBUGGING
    Serial.println("*** XML output end ***");
  #endif
}

//----------------------------------------------------------------------------------------------------
void setup()
{
  #ifdef MYDOOROPENER_SERIAL_DEBUGGING
    Serial.begin(9600);
    Serial.println("*** MyDoorOpener setup begin ***");
  #endif

  if (DOOR1_STATUS_PIN != STATUS_FEEDBACK_DISABLED)
    pinMode(DOOR1_STATUS_PIN, INPUT);

  if (DOOR2_STATUS_PIN != STATUS_FEEDBACK_DISABLED)
    pinMode(DOOR2_STATUS_PIN, INPUT);

  if (DOOR3_STATUS_PIN != STATUS_FEEDBACK_DISABLED)
    pinMode(DOOR3_STATUS_PIN, INPUT);

  if (DOOR1_RELAY_PIN != RELAY_DISABLED)
    pinMode(DOOR1_RELAY_PIN, OUTPUT);

  if (DOOR2_RELAY_PIN != RELAY_DISABLED)
    pinMode(DOOR2_RELAY_PIN, OUTPUT);

  if (DOOR3_RELAY_PIN != RELAY_DISABLED)
    pinMode(DOOR3_RELAY_PIN, OUTPUT);

  // set arbitrary time - used for always-changing challenge token generation

  setTime(0, 0, 0, 1, 1, 2010);

  // start web server

  Ethernet.begin(mac, ip);

  webserver.setDefaultCommand(&webRequestHandler);
  webserver.addCommand("", &webRequestHandler);
  webserver.begin();

  #ifdef MYDOOROPENER_SERIAL_DEBUGGING
    Serial.println("*** MyDoorOpener setup completed ***");
  #endif
}

//----------------------------------------------------------------------------------------------------
void loop()
{
  char buffer[256];
  int len = sizeof(buffer);

  webserver.processConnection(buffer, &len);
}

