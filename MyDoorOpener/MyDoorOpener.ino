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
//
// v2.0 [05/11/2012] - Updated to support unlimited number of devices (used to be max 3).
//                   - Updated to support Arduino v1.0 IDE.
//
//----------------------------------------------------------------------------------------------------

// Uncomment to turn ON serial debugging

// #define MYDOOROPENER_SERIAL_DEBUGGING 1
// #define WEBDUINO_SERIAL_DEBUGGING 2

#include <Arduino.h>
#include <SPI.h>
#include <Time.h>
#include <Ethernet.h>
#include <WebServer.h>
#include <aes256.h>

//*******************************************************************
//*******************************************************************

// EthernetShield MAC address.

static uint8_t mac[6] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

// EthernetShield IP address (DHCP reserved, never allocated to anyone else).

static uint8_t ip[4] = { 192, 168, 0, 13 };

// password required for operating door [max length = 16] (status fetching doesn't require password)

#define PASSWORD "xxx" // value must match the one defined in iOS app

//*******************************************************************
//*******************************************************************

// Arduino HTTP server listening port number.

WebServer webserver("", 80);

// minimum analog value (0-1023) to consider door status == opened,
// anything below this pseudo-voltage value will be considered status == closed.

#define STATUS_OPEN_TRESHOLD 1000

// status contact should be connected to these analog input pins (anologRead).
// Adjust to match the number of devices you have hooked up (examples provided below in comment) ...

static uint8_t statusPins[1] = { 3 }; // single device at pin #3
//static uint8_t statusPins[2] = { 3, 4 }; // two devices at pins #3 and #4
//static uint8_t statusPins[...] = { 3, 4, 5, ... }; // even more devices at pins, #3, #4, #5, etc ...

// open/close trigger relay should be connected to these digital output pins (digitalWrite).
// Adjust to match the number of devices you have hooked up (examples provided below in comment) ...

static uint8_t relayPins[1] = { 9 }; // single device at pin #9
//static uint8_t relayPins[2] = { 9, 10 }; // two devices at pins #9 and #10
//static uint8_t relayPins[...] = { 9, 10, 11, ... }; // even more devices at pins #9, #10, #11, etc ...

// number of milliseconds relay pin will be held high when triggered.

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

  char relayPinAsString[10];
  memset(&relayPinAsString, 0, sizeof(relayPinAsString));
  int relayPin = -1;

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

      else if (strcmp(name, "relayPin") == 0)
      {
        strcpy(relayPinAsString, value);
        relayPin = atoi(relayPinAsString);
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
        Serial.println("*** password matched ***");
      #endif

      // trigger relay pin and hold it HIGH for the appropriate number of milliseconds

      if (relayPin != -1)
      {
        #ifdef MYDOOROPENER_SERIAL_DEBUGGING
          Serial.println("*** relay triggered ***");
        #endif

        digitalWrite(relayPin, HIGH);
        delay(RELAY_DELAY);
        digitalWrite(relayPin, LOW);
      }
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

  for (int i = 0; i < sizeof(statusPins); ++i)
  {
    output(server, "<status statusPin=\"", false);
    output(server, statusPins[i], false);
    output(server, "\">", false);

    // read current open/close state from status pin

    int status = analogRead(statusPins[i]);

    // write current open/close state to output stream

    output(server, (char*)((status >= STATUS_OPEN_TRESHOLD) ? "Opened" : "Closed"), false);
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

  for (int i = 0; i < sizeof(statusPins); ++i)
    pinMode(statusPins[i], INPUT);

  for (int i = 0; i < sizeof(relayPins); ++i)
    pinMode(relayPins[i], OUTPUT);

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

