//---------------------------------------------------------------------------------------------------------------------
// Copyright (C) 2009 - 2014, MyDoorOpener.com
//---------------------------------------------------------------------------------------------------------------------

#ifndef MyDoorOpenerServer_
#define MyDoorOpenerServer_

//---------------------------------------------------------------------------------------------------------------------
class MyDoorOpenerServer
{
	public:
		MyDoorOpenerServer(char* pPassword, uint8_t pMac[6], uint8_t pIp[4], uint8_t pDns[4] = NULL, uint8_t pGateway[4] = NULL);

		void setup();
		void loop();

	protected:

		void handleWebRequest();

		void readRequest(char* url);

		void extractRequestParams(char* url, char* submitPassword, char* relayPinAsString);
		void extractRequestParam(char* name, char* value, char* submitPassword, char* relayPinAsString);

		boolean processRequest(char* submitPassword, char* relayPinAsString);

		boolean isPasswordValid(char* submitPassword);

		void writeResponse();

		void output(char* data, bool newLine);
		void output(int number, bool newLine);

		char currentChallengeToken[20];
		char password[20];

		uint8_t mac[6];
		uint8_t ip[4];
		uint8_t dns[4];
		uint8_t gateway[4];

		EthernetClient client;
};

#endif
