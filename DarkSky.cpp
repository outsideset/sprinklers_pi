// DarkSky.cpp
// This file manages the retrieval of Weather related information and adjustment of durations
//   from DarkSky

#include "config.h"
#ifdef WEATHER_DARKSKY

#include "DarkSky.h"
#include "core.h"
#include "port.h"
#include <string.h>
#include <stdlib.h>
#include <fstream>
#include "json.hpp"

using json = nlohmann::json;

DarkSky::DarkSky(void)
{
	 m_darkSkyAPIHost="api.darksky.net";
}

static void ParseResponse(json &data, Weather::ReturnVals * ret)
{
	freeMemory();
	ret->valid = false;
	ret->maxhumidity = -999;
	ret->minhumidity = 999;
	ret->forecast_maxtempi = 0;
	ret->maxtempi = 0;

	//float temp=0;
	float wind=0;
	float precip=0;
	float uv=0;
	short humidity;

	try {

	    int index = 0;
            for (auto &d : data["daily"]["data"]) {
	        if (index < 2) {
                   precip += d["precipIntensity"].get<float>() * 24.0;
                }
	        trace("index: %d, precip: %0.2f\n", index, precip);
                index++;
            }

		auto & today = data["daily"]["data"][0];
		wind = today["windSpeed"].get<float>();
		//precip = today["precipIntensity"].get<float>() * 24.0;		
		uv = today["uvIndex"].get<float>();		

		auto low = (short) today["temperatureLow"].get<float>();

		ret->maxtempi = (short) today["temperatureHigh"].get<float>();
		ret->meantempi = (ret->maxtempi + (short) low) / 2;
		ret->forecast_maxtempi = ret->maxtempi; // TODO: get forecast. HARDCODE FOR NOW
		ret->windmph = (short) std::round(wind * WIND_FACTOR);
		ret->precip_today = (short) std::round(precip * PRECIP_FACTOR); // we want total not average
		ret->UV = (short) std::round(uv * UV_FACTOR);
		humidity = (short) std::round(today["humidity"].get<float>() * 100.0);
		ret->maxhumidity = ret->minhumidity =  humidity;
		ret->valid = true;
	} catch(std::exception &err) {
		trace(err.what());
	}


	if (ret->maxhumidity == -999 || ret->maxhumidity > 100) {
		ret->maxhumidity = NEUTRAL_HUMIDITY;
	}
	if (ret->minhumidity == 999 || ret->minhumidity < 0) {
		ret->minhumidity = NEUTRAL_HUMIDITY;
	}

	trace("Parsed the following values:\ntemp: %d\nwind: %0.2f\nprecip: %0.2f\nuv: %0.2f\n",
			ret->meantempi, ret->windmph/WIND_FACTOR, ret->precip_today/PRECIP_FACTOR, ret->UV/UV_FACTOR);
}

static void GetData(const Weather::Settings & settings,const char *m_darkSkyAPIHost,time_t timstamp, Weather::ReturnVals * ret)
{
	char cmd[255];

	if (timstamp != 0) {
	   snprintf(cmd, sizeof(cmd), "/usr/bin/curl -sS -o /tmp/darksky.json 'https://%s/forecast/%s/%s,%ld?exclude=currently,hourly,minutely,flags'", m_darkSkyAPIHost, settings.apiSecret, settings.location, timstamp);
        } else {
	   snprintf(cmd, sizeof(cmd), "/usr/bin/curl -sS -o /tmp/darksky.json 'https://%s/forecast/%s/%s?exclude=currently,hourly,minutely,flags'", m_darkSkyAPIHost, settings.apiSecret, settings.location);
        }
	trace("cmd: %s\n",cmd);
	
	FILE *fh;
	char buf[255];
	
	buf[0]=0;
	
	if ((fh = popen(cmd, "r")) != NULL) {
	    size_t byte_count = fread(buf, 1, sizeof(buf) - 1, fh);
	    buf[byte_count] = 0;
	}
	
	(void) pclose(fh);
        if (strlen(buf) > 0) trace("curl error output: %s\n",buf);

	json j;
	std::ifstream ifs("/tmp/darksky.json");
	ifs >> j;
	
	ParseResponse(j, ret);

	ifs.close();
	
	if (!ret->valid)
	{
		if (ret->keynotfound)
			trace("Invalid DarkSky Key\n");
		else
			trace("Bad DarkSky Response\n");
	}
}

Weather::ReturnVals DarkSky::InternalGetVals(const Weather::Settings & settings) const
{
	Weather::ReturnVals minus_two = {0};
	Weather::ReturnVals minus_one = {0};
	Weather::ReturnVals today = {0};
	Weather::ReturnVals tomorrow = {0};
	const time_t 	local_now = nntpTimeServer.LocalNow();

	trace("Get Weather for recent days\n");
	GetData(settings, m_darkSkyAPIHost, local_now - 2 * 24 * 3600, &minus_two);
	GetData(settings, m_darkSkyAPIHost, local_now - 24 * 3600, &minus_one);
	GetData(settings, m_darkSkyAPIHost, 0, &today);
	//GetData(settings, m_darkSkyAPIHost, local_now + 24 * 3600, &tomorrow);

	if (!minus_two.valid || !minus_one.valid || !today.valid) // || !tomorrow.valid) 
        {
		trace("Get Weather FAILED\n");
		return Weather::ReturnVals();
	}

	today.forecast_maxtempi = tomorrow.maxtempi;
	today.precipi = minus_two.precip_today + minus_one.precip_today;
// + tomorrow.precip_today;  
	
	return today;
}

#endif

