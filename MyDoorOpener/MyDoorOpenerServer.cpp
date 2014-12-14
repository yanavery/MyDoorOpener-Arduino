//---------------------------------------------------------------------------------------------------------------------
// Copyright (C) 2009 - 2014, MyDoorOpener.com
//---------------------------------------------------------------------------------------------------------------------

#include <Ethernet.h>
#include <SPI.h>
#include "MyDoorOpenerServer.h"
#include <aes256.h>

extern EthernetServer server;
extern uint8_t *statusPins;
extern boolean isOpen(int pinNumber);
extern int RELAY_DELAY;

//---------------------------------------------------------------------------------------------------------------------
MyDoorOpenerServer::MyDoorOpenerServer(char* pPassword, uint8_t pMac[6], uint8_t pIp[4], uint8_t pDns[4], uint8_t pGateway[4])
{
	memset(&currentChallengeToken, 0, sizeof(currentChallengeToken));

	memset(&password, 0, sizeof(password));
	strncpy(password, pPassword, sizeof(password) - 1);

	for (int i = 0; i < sizeof(mac); i++)
		mac[i] = pMac[i];

	for (int i = 0; i < sizeof(ip); i++)
		ip[i] = pIp[i];

	if (pDns != NULL)
	{
		for (int i = 0; i < sizeof(dns); i++)
			dns[i] = pDns[i];
	}

	if (pGateway != NULL)
	{
		for (int i = 0; i < sizeof(gateway); i++)
			gateway[i] = pGateway[i];
	}
}

//---------------------------------------------------------------------------------------------------------------------
void MyDoorOpenerServer::setup()
{
	#if defined(MYDOOROPENER_SERIAL_DEBUGGING)
		Serial.begin(9600);
		while (!Serial)
		{
			; // wait for serial port to connect
		}
		Serial.println(F("setup begin."));
	#endif

	// start web server

	if (dns != NULL)
	{
		if (gateway != NULL)
			Ethernet.begin(mac, ip, dns, gateway);
		else
			Ethernet.begin(mac, ip, dns);
	}
	else
		Ethernet.begin(mac, ip);

	server.begin();

	#if defined(MYDOOROPENER_SERIAL_DEBUGGING)
		Serial.print(F("server listening @ "));
		Serial.println(Ethernet.localIP());
		Serial.println(F("setup end."));
	#endif
}

//---------------------------------------------------------------------------------------------------------------------
void MyDoorOpenerServer::loop()
{
	// handle web request if one is available

	handleWebRequest();
}

//---------------------------------------------------------------------------------------------------------------------
void MyDoorOpenerServer::handleWebRequest()
{
	client = server.available();
	if (client)
	{
		#if defined(MYDOOROPENER_SERIAL_DEBUGGING)
			Serial.println(F("client request handling begin."));
		#endif

		char request[200];
		memset(&request, 0, sizeof(request));

		// read client request

		readRequest(request);

		#if defined(MYDOOROPENER_SERIAL_DEBUGGING)
			Serial.print(F("request: '"));
			Serial.print(request);
			Serial.println(F("'."));
		#endif

		// only handle GET requests

		if (strncmp("GET ", request, 3) == 0 && !strstr(request, "favicon.ico") && !strstr(request, "robots.txt"))
		{
			char *url = request + 4;

			char submitPassword[33];
			memset(&submitPassword, 0, sizeof(submitPassword));

			char relayPin[10];
			memset(&relayPin, 0, sizeof(relayPin));

			extractRequestParams(url, submitPassword, relayPin);

			#if defined(MYDOOROPENER_SERIAL_DEBUGGING)
				Serial.print(F("submitPassword: '"));
				Serial.print(submitPassword);
				Serial.println(F("'."));

				Serial.print(F("relayPin: '"));
				Serial.print(relayPin);
				Serial.println(F("'."));
			#endif

			processRequest(submitPassword, relayPin);

			#if defined(MYDOOROPENER_SERIAL_DEBUGGING)
				Serial.print(F("token before: '"));
				Serial.print(currentChallengeToken);
				Serial.println(F("'."));
			#endif

			sprintf(currentChallengeToken, "Cyber%lu", millis());

			#if defined(MYDOOROPENER_SERIAL_DEBUGGING)
				Serial.print(F("token after: '"));
				Serial.print(currentChallengeToken);
				Serial.println(F("'."));
			#endif

			// write response

			writeResponse();

			// give the web browser time to receive data before closing

			delay(1);
		}

		client.stop();

		#if defined(MYDOOROPENER_SERIAL_DEBUGGING)
			Serial.println(F("client request handling end."));
		#endif
	}	
}

//---------------------------------------------------------------------------------------------------------------------
void MyDoorOpenerServer::readRequest(char* url)
{
	while (client.connected())
	{
		if (client.available())
		{
			char c = client.read();

			if (c == '\n')
				break;

			url[strlen(url)] = c;
		}
	}
}

//---------------------------------------------------------------------------------------------------------------------
void MyDoorOpenerServer::extractRequestParams(char* url, char* submitPassword, char* relayPinAsString)
{
	char name[17];
	memset(&name, 0, sizeof(name));

	char value[65];
	memset(&value, 0, sizeof(value));

	boolean handlingName = false;
	boolean handlingValue = false;

	for (int i = 0; i < strlen(url); i++)
	{
		char c = url[i];

		if (c == '&' || c == '?')
		{
			handlingName = true;
			handlingValue = false;

			extractRequestParam(name, value, submitPassword, relayPinAsString);

			memset(&name, 0, sizeof(name));
			memset(&value, 0, sizeof(value));
		}
		else if (c == '=')
		{
			handlingName = false;
			handlingValue = true;
		}
		else if (c == ' ')
		{
			break;
		}
		else
		{
			if (handlingName)
				name[strlen(name)] = c;
			else if (handlingValue)
				value[strlen(value)] = c;
		}
	}

	extractRequestParam(name, value, submitPassword, relayPinAsString);
}

//---------------------------------------------------------------------------------------------------------------------
void MyDoorOpenerServer::extractRequestParam(char* name, char* value, char* submitPassword, char* relayPinAsString)
{
	// keep hold of submitted encrypted hex password value

	if (strcmp(name, "password") == 0)
		strcpy(submitPassword, value);

	// keep hold of relay pin which should be triggered

	else if (strcmp(name, "relayPin") == 0)
		strcpy(relayPinAsString, value);
}

//---------------------------------------------------------------------------------------------------------------------
boolean MyDoorOpenerServer::processRequest(char* submitPassword, char* relayPinAsString)
{
	boolean triggered = false;

	// process actual request (if any and if authorized)

	if (isPasswordValid(submitPassword) && strlen(relayPinAsString) > 0)
	{
		int relayPin = atoi(relayPinAsString);

		if (relayPin > -1)
		{
			#if defined(MYDOOROPENER_SERIAL_DEBUGGING)
				Serial.println(F("*** relay triggered ***"));
			#endif

			digitalWrite(relayPin, HIGH);
			delay(RELAY_DELAY);
			digitalWrite(relayPin, LOW);

			triggered = true;
		}
	}

	return triggered;
}

//---------------------------------------------------------------------------------------------------------------------
boolean MyDoorOpenerServer::isPasswordValid(char* submitPassword)
{
	boolean isValid = false;

	if(submitPassword && strlen(submitPassword) > 0)
	{
		#if defined(MYDOOROPENER_SERIAL_DEBUGGING)
			Serial.print(F("submitPassword: '"));
			Serial.print(submitPassword);
			Serial.println(F("'."));
		#endif

		// decrypt password using latest challenge token as cypher key

		uint8_t cryptoKey[33];
		memset(&cryptoKey, 0, sizeof(cryptoKey));

		for (int i = 0; i < strlen(currentChallengeToken); i++)
			cryptoKey[i] = currentChallengeToken[i];

		uint8_t passwordAsLong[20];
		memset(&passwordAsLong, 0, sizeof(passwordAsLong));

		// convert password from hex string to ascii decimal

		int i = 0;
		int j = 0;
		while (true)
		{
			if (!submitPassword[j])
				break;

			char hexValue[3] = { submitPassword[j], submitPassword[j+1], '\0' };
			passwordAsLong[i] = (int) strtol(hexValue, NULL, 16);

			i += 1;
			j += 2;
		}

		// proceed with AES256 password decryption

		aes256_context ctx;
		aes256_init(&ctx, cryptoKey);
		aes256_decrypt_ecb(&ctx, passwordAsLong);
		aes256_done(&ctx);

		char passwordAsChar[20];
		memset(&passwordAsChar, 0, sizeof(passwordAsChar));

		for (int i = 0; i < sizeof(passwordAsLong); i++)
			passwordAsChar[i] = passwordAsLong[i];

		#if defined(MYDOOROPENER_SERIAL_DEBUGGING)
			Serial.print(F("passwordAsChar: '"));
			Serial.print(passwordAsChar);
			Serial.println(F("'."));
		#endif

		// if password matches, trigger relay

		if (strcmp(passwordAsChar, password) == 0)
			isValid = true;
	}

	#if defined(MYDOOROPENER_SERIAL_DEBUGGING)
		Serial.print(F("Password: '"));
		Serial.print(isValid ? F("OK") : F("KO"));
		Serial.println(F("'."));
	#endif

	return isValid;
}

//---------------------------------------------------------------------------------------------------------------------
void MyDoorOpenerServer::writeResponse()
{
	// write HTTP headers

	output("HTTP/1.1 200 OK", true);
	output("Content-Type: text/xml", true);
	output("Connection: close", true);
	output("", true);

	// write opening XML element (body payload begin)

	output("<?xml version=\"1.0\"?>", true);
	output("<myDoorOpener>", true);

	// write current door status

	for (int i = 0; i < sizeof(statusPins); ++i)
	{
		output("<status statusPin=\"", false);
		output(statusPins[i], false);
		output("\">", false);

		// write current open/close state to output stream

		output((char*)((isOpen(statusPins[i])) ? "Opened" : "Closed"), false);
		output("</status>", true);
	}

	// write challenge token to output stream

	output("<challengeToken>", false);
	output(currentChallengeToken, false);
	output("</challengeToken>", true);

	// write closing XML element (body payload end)

	output("</myDoorOpener>", true);
}

//---------------------------------------------------------------------------------------------------------------------
void MyDoorOpenerServer::output(char* data, bool newLine)
{
	#if defined(MYDOOROPENER_SERIAL_DEBUGGING)
		if (newLine)
			Serial.println(data);
		else
			Serial.print(data);
	#endif

	if (newLine)
		client.println(data);
	else
		client.print(data);
}

//---------------------------------------------------------------------------------------------------------------------
void MyDoorOpenerServer::output(int number, bool newLine)
{
	char str[10] = "";
	itoa(number, str, 10);

	output(str, newLine);
}
