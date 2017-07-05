#include <user_config.h>
#include <SmingCore/SmingCore.h>
#include <AppSettings.h>
#include <Adafruit_TCS34725/Adafruit_TCS34725.h>
#include <Adafruit_NeoPixel/Adafruit_NeoPixel.h>

// Which pin on the Esp8266 is connected to the NeoPixels?
#define PIN     15
// How many NeoPixels are attached to the Esp8266?
#define NUMPIXELS 12
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

// Define ColorSensor
Adafruit_TCS34725 tcs = Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_4X);
#define SEUIL 20

HttpServer server;

BssList networks;
String network, password;
Timer connectionTimer;
Timer runTimer;

// our RGB -> eye-recognized gamma color
byte gammatable[256];

void setPixels(float r, float g, float b);
void startWebServer();
void networkScanCompleted(bool succeeded, BssList list);
void setup();
void loop();

void init() {
	setup();
	runTimer.initializeMs(1000,loop).start();  
}

void setup() {
	Serial.begin(115200);
	strip.begin();
	setPixels(127,127,127);
	Wire.pins(5,4); // Added for pin compatibility with Wemos D1 documentation
	if (tcs.begin()) Serial.println("Found sensor");
	// thanks PhilB for this gamma table!
	// it helps convert RGB colors to what humans see
	for (int i=0; i<256; i++) {
		float x = i;
		x /= 255;
		x = pow(x, 2.5);
		x *= 255;

		gammatable[i] = x;
		Serial.println(gammatable[i]);
	}

	spiffs_mount(); // Mount file system, in order to work with files

	AppSettings.load();

	WifiStation.enable(true);

	if (AppSettings.exist())
	{
		WifiStation.config(AppSettings.ssid, AppSettings.password);
		if (!AppSettings.dhcp && !AppSettings.ip.isNull())
			WifiStation.setIP(AppSettings.ip, AppSettings.netmask, AppSettings.gateway);
	}

	WifiStation.startScan(networkScanCompleted);

	// Start AP for configuration
	WifiAccessPoint.enable(true);
	WifiAccessPoint.config("Sming Configuration", "", AUTH_OPEN);

	// Run WEB server on system ready
	System.onReady(startWebServer);
}

void loop() {
	uint16_t clear, red, green, blue;

	tcs.setInterrupt(false);      // turn on LED

	delay(60);  // takes 50ms to read 

	tcs.getRawData(&red, &green, &blue, &clear);

	tcs.setInterrupt(true);  // turn off LED

	// Figure out some basic hex code for visualization
	uint32_t sum = clear;
	static float r, g, b, Pr, Pg, Pb;
	r = red; r /= sum;
	g = green; g /= sum;
	b = blue; b /= sum;
	r *= 256; g *= 256; b *= 256;

	Serial.print("C: "); Serial.print(clear);
	Serial.print("\tR: "); Serial.print(red);
	Serial.print("\tG: "); Serial.print(green);
	Serial.print("\tB: "); Serial.print(blue);
	Serial.print("\t");
	Serial.print((char)r, HEX); Serial.print((char)g, HEX); Serial.print((char)b, HEX);
	Serial.println(); 

	if( (abs(Pr-r)>SEUIL) || (abs(Pg-g)>SEUIL) || (abs(Pb-b)>SEUIL) )
	{
		setPixels(r,g,b);
		Pr=r; Pg=g; Pb=b;
	}
}

void setPixels(float r, float g, float b) {
	int red,green,blue;
	const float coef=2.5;
	red = gammatable[(int)r] * coef;
	green = gammatable[(int)g] * coef;
	blue = gammatable[(int)b] * coef;


	for(int i=0;i<NUMPIXELS;i++){
		strip.setPixelColor(i,strip.Color(red,green,blue));
		strip.show();
		delay(5);
	}
}

void onIndex(HttpRequest &request, HttpResponse &response)
{
	TemplateFileStream *tmpl = new TemplateFileStream("index.html");
	auto &vars = tmpl->variables();
	response.sendTemplate(tmpl); // will be automatically deleted
}

void onIpConfig(HttpRequest &request, HttpResponse &response)
{
	if (request.method == HTTP_POST)
	{
		AppSettings.dhcp = request.getPostParameter("dhcp") == "1";
		AppSettings.ip = request.getPostParameter("ip");
		AppSettings.netmask = request.getPostParameter("netmask");
		AppSettings.gateway = request.getPostParameter("gateway");
		debugf("Updating IP settings: %d", AppSettings.ip.isNull());
		AppSettings.save();
	}

	TemplateFileStream *tmpl = new TemplateFileStream("settings.html");
	auto &vars = tmpl->variables();

	bool dhcp = WifiStation.isEnabledDHCP();
	vars["dhcpon"] = dhcp ? "checked='checked'" : "";
	vars["dhcpoff"] = !dhcp ? "checked='checked'" : "";

	if (!WifiStation.getIP().isNull())
	{
		vars["ip"] = WifiStation.getIP().toString();
		vars["netmask"] = WifiStation.getNetworkMask().toString();
		vars["gateway"] = WifiStation.getNetworkGateway().toString();
	}
	else
	{
		vars["ip"] = "192.168.1.77";
		vars["netmask"] = "255.255.255.0";
		vars["gateway"] = "192.168.1.1";
	}

	response.sendTemplate(tmpl); // will be automatically deleted
}

void onFile(HttpRequest &request, HttpResponse &response)
{
	String file = request.getPath();
	if (file[0] == '/')
		file = file.substring(1);

	if (file[0] == '.')
		response.forbidden();
	else
	{
		response.setCache(86400, true); // It's important to use cache for better performance.
		response.sendFile(file);
	}
}

void onAjaxNetworkList(HttpRequest &request, HttpResponse &response)
{
	JsonObjectStream* stream = new JsonObjectStream();
	JsonObject& json = stream->getRoot();

	json["status"] = (bool)true;

	bool connected = WifiStation.isConnected();
	json["connected"] = connected;
	if (connected)
	{
		// Copy full string to JSON buffer memory
		json["network"]= WifiStation.getSSID();
	}

	JsonArray& netlist = json.createNestedArray("available");
	for (int i = 0; i < networks.count(); i++)
	{
		if (networks[i].hidden) continue;
		JsonObject &item = netlist.createNestedObject();
		item["id"] = (int)networks[i].getHashId();
		// Copy full string to JSON buffer memory
		item["title"] = networks[i].ssid;
		item["signal"] = networks[i].rssi;
		item["encryption"] = networks[i].getAuthorizationMethodName();
	}

	response.setAllowCrossDomainOrigin("*");
	response.sendDataStream(stream, MIME_JSON);
}

void makeConnection()
{
	WifiStation.enable(true);
	WifiStation.config(network, password);

	AppSettings.ssid = network;
	AppSettings.password = password;
	AppSettings.save();

	network = ""; // task completed
}

void onAjaxConnect(HttpRequest &request, HttpResponse &response)
{
	JsonObjectStream* stream = new JsonObjectStream();
	JsonObject& json = stream->getRoot();

	String curNet = request.getPostParameter("network");
	String curPass = request.getPostParameter("password");

	bool updating = curNet.length() > 0 && (WifiStation.getSSID() != curNet || WifiStation.getPassword() != curPass);
	bool connectingNow = WifiStation.getConnectionStatus() == eSCS_Connecting || network.length() > 0;

	if (updating && connectingNow)
	{
		debugf("wrong action: %s %s, (updating: %d, connectingNow: %d)", network.c_str(), password.c_str(), updating, connectingNow);
		json["status"] = (bool)false;
		json["connected"] = (bool)false;
	}
	else
	{
		json["status"] = (bool)true;
		if (updating)
		{
			network = curNet;
			password = curPass;
			debugf("CONNECT TO: %s %s", network.c_str(), password.c_str());
			json["connected"] = false;
			connectionTimer.initializeMs(1200, makeConnection).startOnce();
		}
		else
		{
			json["connected"] = WifiStation.isConnected();
			debugf("Network already selected. Current status: %s", WifiStation.getConnectionStatusName());
		}
	}

	if (!updating && !connectingNow && WifiStation.isConnectionFailed())
		json["error"] = WifiStation.getConnectionStatusName();

	response.setAllowCrossDomainOrigin("*");
	response.sendDataStream(stream, MIME_JSON);
}

void startWebServer()
{
	server.listen(80);
	server.addPath("/", onIndex);
	server.addPath("/ipconfig", onIpConfig);
	server.addPath("/ajax/get-networks", onAjaxNetworkList);
	server.addPath("/ajax/connect", onAjaxConnect);
	server.setDefaultHandler(onFile);
}

void networkScanCompleted(bool succeeded, BssList list)
{
	if (succeeded)
	{
		for (int i = 0; i < list.count(); i++)
			if (!list[i].hidden && list[i].ssid.length() > 0)
				networks.add(list[i]);
	}
	networks.sort([](const BssInfo& a, const BssInfo& b){ return b.rssi - a.rssi; } );
}
