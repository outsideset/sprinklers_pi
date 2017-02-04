// Weather.cpp
// This file manages the retrieval of Weather related information and adjustment of durations
//   from Weather Underground
// Author: Richard Zimmerman
// Copyright (c) 2013 Richard Zimmerman
//

#include "Weather.h"
#include "core.h"
#include "port.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>


Weather::Weather(void)
{
}

static void ParseResponse(EthernetClient & client, Weather::ReturnVals * ret)
{
	freeMemory();
	ret->valid = false;
	enum
	{
		FIND_QUOTE1 = 0, PARSING_KEY, FIND_QUOTE2, PARSING_VALUE, PARSING_QVALUE, ERROR
	} current_state = FIND_QUOTE1;
	char recvbuf[100];
	char * recvbufptr = recvbuf;
	char * recvbufend = recvbuf;
	char key[30], val[30];
	char * keyptr = key;
	char * valptr = val;
	while (true)
	{
		if (recvbufptr >= recvbufend)
		{
			int len = client.read((uint8_t*) recvbuf, sizeof(recvbuf));
//			trace(F("Received Bytes:%d\n"), len);
			if (len <= 0)
			{
				if (!client.connected()) {
					trace("Client Disconnected\n");
					break;
				} else {
					continue;  //TODO:  implement a timeout here.  Same in testing parse headers.
				}
			}
			else
			{
				recvbufptr = recvbuf;
				recvbufend = recvbuf + len;
				//trace(recvbuf);
			}
		}
		char c = *(recvbufptr++);

		switch (current_state)
		{
		case FIND_QUOTE1:
			if (c == '"')
			{
				current_state = PARSING_KEY;
				keyptr = key;
			}
			break;
		case PARSING_KEY:
			if (c == '"')
			{
				current_state = FIND_QUOTE2;
				*keyptr = 0;
			}
			else
			{
				if ((keyptr - key) < (long)(sizeof(key) - 1))
				{
					*keyptr = c;
					keyptr++;
				}
			}
			break;
		case FIND_QUOTE2:
			if (c == '"')
			{
				current_state = PARSING_QVALUE;
				valptr = val;
			}
			else if (c == '{')
			{
				current_state = FIND_QUOTE1;
			}
			else if ((c >= '0') && (c <= '9'))
			{
				current_state = PARSING_VALUE;
				valptr = val;
				*valptr = c;
				valptr++;
			}
			break;
		case PARSING_VALUE:
			if (((c >= '0') && (c <= '9')) || (c == '.'))
			{
				*valptr = c;
				valptr++;
			}
			else
			{
				current_state = FIND_QUOTE1;
				*valptr = 0;
			}
			break;
		case PARSING_QVALUE:
			if (c == '"')
			{
				current_state = FIND_QUOTE1;
				*valptr = 0;
				//trace("%s:%s\n", key, val);
				if (strcmp(key, "maxhumidity") == 0)
				{
					ret->valid = true;
					ret->keynotfound = false;
					ret->maxhumidity = atoi(val);
				}
				else if (strcmp(key, "minhumidity") == 0)
				{
					ret->minhumidity = atoi(val);
				}
				else if (strcmp(key, "maxtempi") == 0)
				{
					ret->maxtempi = atoi(val);
				}
				else if (strcmp(key, "meantempi") == 0)
				{
					ret->meantempi = atoi(val);
				}
				else if (strcmp(key, "fahrenheit") == 0)
				{
				// assume the first F value is today's forecast high
			   	  if (ret->forecast_maxtempi == 0)
				  {
				   trace(F("Fahrenheit!!!(%d)\n"), atoi(val));
				   ret->forecast_maxtempi = atoi(val);
				  }

				}
				else if (strcmp(key, "precip_today_in") == 0)
				{
					ret->precip_today = (atof(val) * 100.0);
				}
				else if (strcmp(key, "precipi") == 0)
				{
					ret->precipi = (atof(val) * 100.0);
				}
				else if (strcmp(key, "UV") == 0)
				{
					ret->UV = (atof(val) * 10.0);
				}
				else if (strcmp(key, "meanwindspdi") == 0)
				{
					ret->windmph = (atof(val) * 10.0);
				}
				else if (strcmp(key, "type") == 0)
				{
					if (strcmp(val, "keynotfound") == 0)
						ret->keynotfound = true;
				}

			}
			else
			{
				if ((valptr - val) < (long)(sizeof(val) - 1))
				{
					*valptr = c;
					valptr++;
				}
			}
			break;
		case ERROR:
			break;
		} // case
	} // while (true)
}

int Weather::GetScale(time_t local_now, const IPAddress & ip, const char * key, uint32_t zip, const char * pws, bool usePws) const
{
	ReturnVals vals = GetVals(ip, key, zip, pws, usePws);
	return GetScale(local_now, vals);
}

/*int Weather::GetScale(const ReturnVals & vals) const
{
	if (!vals.valid)
		return 100;
	const int humid_factor = 70 - (vals.maxhumidity + vals.minhumidity) / 2;
	const int temp_factor = (vals.meantempi - 70) * 4;
	const int rain_factor = (vals.precipi + vals.precip_today) * -2;
	const int adj = min(max(0, 100+humid_factor+temp_factor+rain_factor), 200);
	trace(F("Adjusting H(%d)T(%d)R(%d):%d\n"), humid_factor, temp_factor, rain_factor, adj);
	return adj;
}*/

static const int HUMIDITY_MULTIPLIER = 0.5;
static const int HUMIDITY_BIAS = -7;

static const int TEMP_MULTIPLIER = 2.0;

static const int ON_MONTH_CUTOFF = 10;
static const int ON_MONTH_TEMP_BIAS = 3;

static const int OFF_MONTH_CUTOFF = 20;
//static const int OFF_MONTH_TEMP_BIAS_ADJUSTMENT = 10;
//static const int OFF_MONTH_TEMP_BIAS = 
//    ((100 - OFF_MONTH_CUTOFF) / TEMP_MULTIPLIER) +
//     OFF_MONTH_TEMP_BIAS_ADJUSTMENT;
static const int OFF_MONTH_TEMP_BIAS = 58;

static bool IsOnMonth (time_t local_now)
{
        static bool onMonths[] =
        {
		false, false, true, true,
		true, true, true, true,
		true, true, true, true
        };
	const int month_index = month(local_now) - 1;
	//trace(F("IsOnMonth=%d, month index=%d\n"), (int) onMonths[month_index], month_index);
        return onMonths[month_index];
}

static int GetMonthlyAdjustmentCutoff (time_t local_now) 
{
        const int val = IsOnMonth(local_now) ? ON_MONTH_CUTOFF : OFF_MONTH_CUTOFF;
	trace(F("Monthly Adjustment Cutoff(%d)\n"), val);
	return val;
}

static int GetMonthlyAverageHumidity (time_t local_now) 
{
        static int monthly[] =
        {
		77, 78, 77, 76,
		75, 75, 76, 76,
		75, 73, 73, 73 
        };

	const int month_index = month(local_now) - 1;
        const int val = monthly[month_index];
	trace(F("Local Monthly Avg Humidity, Bias(%d,%d)\n"), val, HUMIDITY_BIAS);
	return val + HUMIDITY_BIAS;
}

static int GetMonthlyAverageTemperature (time_t local_now) 
{
        static int monthlyAvg[] =
        {
                61, 62, 64, 66,
                69, 73, 76, 77,
                77, 74, 67, 61 
        };
	const int month_index = month(local_now) - 1;
        bool is_on_month = IsOnMonth(local_now);
	const int bias = is_on_month ? ON_MONTH_TEMP_BIAS : OFF_MONTH_TEMP_BIAS;
        const int avg = monthlyAvg[month_index];
	trace(F("Local Monthly Avg Temp,Bias(%d,%d) %s\n"), avg, bias, is_on_month ? "ON MONTH" : "OFF MONTH");
	return avg + bias;
}

int Weather::GetScale(time_t local_now, const ReturnVals & vals) const
{
        int defaultHumidity = GetMonthlyAverageHumidity(local_now);
        int monthlyAvg = GetMonthlyAverageTemperature(local_now);
	if (!vals.valid)
		return 100;
        const int avgHum = (vals.maxhumidity + vals.minhumidity) / 2;
	const int humid_factor = (defaultHumidity - avgHum) * HUMIDITY_MULTIPLIER; 
	const int temp_factor = (int)(TEMP_MULTIPLIER *
		(((vals.forecast_maxtempi + vals.maxtempi) / 2.0) - monthlyAvg));
	const int rain_factor = (vals.precipi + vals.precip_today) * -2;
	int adj = min(max(0, 100+humid_factor+temp_factor+rain_factor), 200);
	trace(F("WundergroundG temp(y=%d,t=%d) precip(y=%d, t=%d) avghum(%d)\n"), 
vals.maxtempi, vals.forecast_maxtempi, vals.precipi, vals.precip_today, avgHum);
	trace(F("Adjusting H(%d)T(%d)R(%d):%d\n"), humid_factor, temp_factor, rain_factor, adj);

   	const int adjFloor = GetMonthlyAdjustmentCutoff(local_now);
	if (adj < adjFloor) {
	   trace(F("!!! adj(%d) < cutoff(%d). Flooring !!!\n"), adj, adjFloor);
	   adj = 0;
 	}
	else if (!IsOnMonth(local_now)) {
	   trace(F("!!! Sprinklers activating in off month !!!\n"));
	}
	return adj;
}


static Weather::ReturnVals __GetVals(const IPAddress & ip, const char * key, uint32_t zip, const char * pws, bool usePws) 
{
	Weather::ReturnVals vals = {0};
	EthernetClient client;
	if (client.connect(ip, 80))
	{
		char getstring[90];
		trace(F("Connected\n"));
		if (usePws)
			snprintf(getstring, sizeof(getstring), "GET /api/%s/yesterday/conditions/q/pws:%s.json HTTP/1.1\r\n", key, pws);
		else
			snprintf(getstring, sizeof(getstring), "GET /api/%s/yesterday/conditions/forecast/q/%ld.json HTTP/1.1\r\n", key, (long) zip);
		//	snprintf(getstring, sizeof(getstring), "GET /api/%s/yesterday/conditions/q/%ld.json HTTP/1.1\r\n", key, (long) zip);
		trace(getstring);
		client.write((uint8_t*) getstring, strlen(getstring));
		//send host header
		snprintf(getstring, sizeof(getstring), "Host: api.wunderground.com\r\nConnection: close\r\n\r\n");
		client.write((uint8_t*) getstring, strlen(getstring));


		//send host header
		snprintf(getstring, sizeof(getstring), "Host: api.wunderground.com\r\nConnection: close\r\n\r\n");
		client.write((uint8_t*) getstring, strlen(getstring));


		ParseResponse(client, &vals);
		client.stop();
		if (!vals.valid)
		{
			if (vals.keynotfound)
				trace("Invalid WUnderground Key\n");
			else
				trace("Bad WUnderground Response\n");
		}
	}
	else
	{
		trace(F("connection failed\n"));
		client.stop();
	}
	return vals;
}

Weather::ReturnVals Weather::GetVals(const IPAddress & ip, const char * key, uint32_t zip, const char * pws, bool usePws) const
{
    trace(F("calling Wunderground with retries\n"));
    Weather::ReturnVals vals = {0};
    for (int retries = 0; retries < 5; retries++)
    {
	vals = __GetVals(ip, key, zip, pws, usePws);
        if (vals.valid) break;
        usleep(100000);  // sleep for 100 ms
        trace(F("Wunderground connection failed. RETRYING up to 5 times\n"));
    }
    return vals;
}

