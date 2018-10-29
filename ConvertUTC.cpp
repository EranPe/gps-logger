#include <SoftwareSerial.h>
#include <SPI.h>

class ConvertUTC
{

  public:

  const char* UTC[40] = {"UTC-12:00","UTC-11:00","UTC-10:00","UTC-09:30","UTC-09:00",
  "UTC-08:00","UTC-07:00","UTC-06:00","UTC-05:00","UTC-04:00","UTC-03:30","UTC-03:00",
  "UTC-02:00","UTC-01:00","UTC","UTC+01:00","UTC+02:00","UTC+03:00","UTC+03:30",
  "UTC+04:00","UTC+04:30","UTC+05:00","UTC+05:30","UTC+05:45","UTC+06:00","UTC+06:30",
  "UTC+07:00","UTC+08:00","UTC+08:30","UTC+08:45","UTC+09:00","UTC+09:30","UTC+10:00",
  "UTC+10:30","UTC+11:00","UTC+12:00","UTC+12:45","UTC+13:00","UTC+13:45","UTC+14:00"};

  // Return date and time in the format: YYMMDDHHMMSS
  // utcDate - Raw date in DDMMYY format (u32)
  // utcTime - Raw time in HHMMSSCC format (u32)
  // Number of days in a month
  // Returns the date and the time in RAW format: YYMMDDHHMMSS
  static String localTime(int utcDate, int utcTime, float TimeZone, int DST, bool leapYear)
  {
    int DaysAMonth[] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  
    int utcYear = utcDate % 100;
    int utcMonth = (utcDate / 100) % 100;
    int utcDay = (utcDate / 10000);
    byte utcHour = (utcTime / 1000000);
    byte utcMin = (utcTime / 10000) % 100;
    byte utcSec = (utcTime / 100) % 100;
  
    String strDate, strTime;
    
    strDate = String(utcYear / 10);
    strDate += String(utcYear % 10);
    strDate += String(utcMonth / 10);
    strDate += String(utcMonth % 10);
    strDate += String(utcDay / 10);
    strDate += String(utcDay % 10);
  
    strTime = String(utcHour / 10);
    strTime += String(utcHour % 10);
    strTime += String(utcMin / 10);
    strTime += String(utcMin % 10);
    strTime += String(utcSec / 10);
    strTime += String(utcSec % 10);
  
    byte localYear = utcYear;
    byte localMonth = utcMonth;
    byte localDay = utcDay;
    byte localHour = utcHour;
    byte localMin = utcMin;
    byte localSec = utcSec;
  
    if (leapYear) DaysAMonth[1] = 29;                               // Leap year check
    
    localHour += (byte)TimeZone;                                    // Time zone adjustment (Hours)
    
    localMin += (byte)((TimeZone - (byte)TimeZone)*60);             // Time zone adjustment (Minutes)
    if (localMin > 59)                                              // If the local minutes time is after the current hour
    {
      localHour++;                                                  // Go to the next hour
      localMin -= 60;                                               // Adjust the minutes accordingly
    }
    else if (localMin < 0)                                          // If the local minutes time is before the current hour
    {
      localHour--;                                                  // Go back to the previous hour
      localMin += 60;                                               // Adjust the minutes accordingly
    }
    
    localHour += DST;                                               // DST adjustment
    
    if (localHour < 0)                                              // Previous day
      {
        localHour += 24;                                            // Adjust the time
        localDay -= 1;                                              // Adjust the day (Previous day)
        if (localDay < 1)                                           // if the local day is previous to the first day of the new the month
        {
          if (localMonth == 1)                                      // If the month is the first month of the year, January, i.e., 1/1
          {
            localMonth = 12;                                        // Go back to December
            localYear -= 1;                                         // Go back to previous year
            if (localYear < 0) localYear = 99;                      // If the local year is before the turn of the new century -> return to year 99
          }
          else                                                      // If the current month isn't January, but still the first of the month
          {
            localMonth -= 1;                                        // Go back to previous month
          }
          localDay = DaysAMonth[localMonth-1];                      // Go back to the last day of the previous month according to the month
        }
      }
      if (localHour >= 24)                                          // Next day
      {
        localHour -= 24;                                            // Adjust the time
        localDay += 1;                                              // Adjust the day (next day)
        if (localDay > DaysAMonth[localMonth-1])                    // If the local day is after the last day of the month
        {
          localDay = 1;                                             // Local day is the first day of the new month
          localMonth += 1;                                          // The new local month is the next month
          if (localMonth > 12)                                      // If the local month is the last month of the year, December, i.e., 31/12
          {
            localMonth = 1;                                         // Go next to January 
            localYear += 1;                                         // Go to the next year 
            if (localYear > 99) localYear = 0;                      // If the local year is after the turn of the century -> make it year 00 (A new century)
          }
        }
      }
  
    strDate = String(localYear / 10);
    strDate += String(localYear % 10);
    strDate += String(localMonth / 10);
    strDate += String(localMonth % 10);
    strDate += String(localDay / 10);
    strDate += String(localDay % 10);
  
    strTime = String(localHour / 10);
    strTime += String(localHour % 10);
    strTime += String(localMin / 10);
    strTime += String(localMin % 10);
    strTime += String(localSec / 10);
    strTime += String(localSec % 10);
  
    return (strDate + strTime);
  }
  
  
  /*
  if (year is not divisible by 4) then (it is a common year)
  else if (year is not divisible by 100) then (it is a leap year)
  else if (year is not divisible by 400) then (it is a common year)
  else (it is a leap year)
   */
  static bool isLeapYear(int year)
  {
    if (year % 4 == 0)
    {
      if (year % 100 == 0)
      {
        if (year % 400 == 0) return true;
        else return false;
      }
      else return true;
    }
    else return false;
  }
};
