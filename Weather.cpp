// Weather.cpp
// This file manages the retrieval of Weather related information and adjustment of durations
// Author: Richard Zimmerman
// Copyright (c) 2013 Richard Zimmerman
//

#include "Weather.h"
#include "core.h"
#include "port.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <algorithm>

using namespace std;

Weather::Settings Weather::GetSettings(void) {
	Settings settings = {0};

	GetApiKey(settings.key);
	GetApiId(settings.apiId);
	GetApiSecret(settings.apiSecret);
	GetPWS(settings.pws);
	GetLoc(settings.location);

	settings.zip = GetZip();
	settings.usePws = GetUsePWS();

	return settings;
}

int16_t Weather::GetScale(time_t local_now) const
{
	ReturnVals vals = this->GetVals();
	return this->GetScale(local_now, vals);
}

int16_t Weather::GetScale(time_t local_now, const Weather::Settings & settings) const
{
	ReturnVals vals = this->GetVals(settings);
	return this->GetScale(local_now, vals);
}

/*
int16_t Weather::GetScale(const ReturnVals & vals) const
{
	if (!vals.valid)
		return 100;
	const int humid_factor = NEUTRAL_HUMIDITY - (vals.maxhumidity + vals.minhumidity) / 2;
	const int temp_factor = (vals.meantempi - 70) * 4;
	const int rain_factor = (vals.precipi + vals.precip_today) * -2;
	const int16_t adj = (uint16_t)spi_min(spi_max(0, 100+humid_factor+temp_factor+rain_factor), 200);
	trace(F("Adjusting H(%d)T(%d)R(%d):%d\n"), humid_factor, temp_factor, rain_factor, adj);
	return adj;
}*/

static const int HUMIDITY_MULTIPLIER = 0.5;
static const int HUMIDITY_BIAS = -7;

static const int TEMP_MULTIPLIER = 2.0;

static const int ON_MONTH_CUTOFF = 10;
static const int ON_MONTH_TEMP_BIAS = 3;

//static const int OFF_MONTH_CUTOFF = 10;
//static const int OFF_MONTH_TEMP_BIAS_ADJUSTMENT = 10;
//static const int OFF_MONTH_TEMP_BIAS = 
//    ((100 - OFF_MONTH_CUTOFF) / TEMP_MULTIPLIER) +
//     OFF_MONTH_TEMP_BIAS_ADJUSTMENT;
//static const int OFF_MONTH_TEMP_BIAS = 58;

/*
bool IsOnMonth (time_t local_now)
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
*/

int GetMonthlyAdjustmentCutoff (time_t local_now) 
{
    return ON_MONTH_CUTOFF;
/*
        const int val = IsOnMonth(local_now) ? ON_MONTH_CUTOFF : OFF_MONTH_CUTOFF;
	trace(F("Monthly Adjustment Cutoff(%d)\n"), val);
	return val;
*/
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
        //bool is_on_month = IsOnMonth(local_now);
	//const int bias = is_on_month ? ON_MONTH_TEMP_BIAS : OFF_MONTH_TEMP_BIAS;
	const int bias = ON_MONTH_TEMP_BIAS;
        const int avg = monthlyAvg[month_index];
	//trace(F("Local Monthly Avg Temp,Bias(%d,%d) %s\n"), avg, bias, is_on_month ? "ON MONTH" : "OFF MONTH");
	trace(F("Local Monthly Avg Temp,Bias(%d,%d)\n"), avg, bias);
	return avg + bias;
}

int16_t Weather::GetScale(time_t local_now, const ReturnVals & vals) const
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
	return adj;
}


// static Weather::ReturnVals __GetVals(const IPAddress & ip, const char * key, uint32_t zip, const char * pws, bool usePws) 
// {
// 	Weather::ReturnVals vals = {0};
// 	EthernetClient client;
// 	if (client.connect(ip, 80))
// 	{
// 		char getstring[90];
// 		trace(F("Connected\n"));
// 		if (usePws)
// 			snprintf(getstring, sizeof(getstring), "GET /api/%s/yesterday/conditions/q/pws:%s.json HTTP/1.1\r\n", key, pws);
// 		else
// 			snprintf(getstring, sizeof(getstring), "GET /api/%s/yesterday/conditions/forecast/q/%ld.json HTTP/1.1\r\n", key, (long) zip);
// 		//	snprintf(getstring, sizeof(getstring), "GET /api/%s/yesterday/conditions/q/%ld.json HTTP/1.1\r\n", key, (long) zip);
// 		trace(getstring);
// 		client.write((uint8_t*) getstring, strlen(getstring));
// 		//send host header
// 		snprintf(getstring, sizeof(getstring), "Host: api.wunderground.com\r\nConnection: close\r\n\r\n");
// 		client.write((uint8_t*) getstring, strlen(getstring));


// 		//send host header
// 		snprintf(getstring, sizeof(getstring), "Host: api.wunderground.com\r\nConnection: close\r\n\r\n");
// 		client.write((uint8_t*) getstring, strlen(getstring));

Weather::ReturnVals Weather::GetVals(void) const
{
	Settings settings = this->GetSettings();
	return this->InternalGetVals(settings);
}

Weather::ReturnVals Weather::GetVals(const Settings & settings) const
{
	return this->InternalGetVals(settings);
}

Weather::ReturnVals Weather::InternalGetVals(const Settings & settings) const
{
	// You must override and implement this function
	trace("Warning: Placeholder weather provider called. No weather scaling used.\n");
	ReturnVals vals = {0};
	vals.valid = false;
	return vals;
}
