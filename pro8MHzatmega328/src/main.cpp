/**************************************************************
  Projet: GPS_Logger, AT août 2021  Version: TOA_08.17.unicsv

  Affichage des données du module GPS sur un écan OLED
  et/ou enregistrement sur une carte SD au format 'unicsv'
  la 1ère ligne du fichier Trajet.csv décrit les champs enregistrés:
  desc,lat,lon,ele,speed    (speed en m/s)
  
  ET doit être présente sur la carte SD que l'on utilise !

  Proc: Arduino Atmega328 pro-mini 3.3volts/8MHz.
  _RAM:   [========= ]  85.3% (used 1747 bytes from 2048 bytes)
  _Flash: [========= ]  85.9% (used 26390 bytes from 30720 bytes)
  
***************************************************************** */

// bibliothèque pour le module GPS Ublox NEO-6
#include <SoftwareSerial.h> // pour avoir un 2ème port série pour le GPS
#include <TinyGPS++.h>	// version 1.0.2b, lien:  https://github.com/mikalhart/TinyGPSPlus
// mais la lib s'appelle "TinyGPSPlus" !!!

// bibliothèques pour l'écran OLED en I2C
//  voir sur le lien pour la config et les fonts, c'est pas triste...
#include <U8g2lib.h>	// version "unique"?, lien: https://github.com/olikraus/u8g2
//#include <U8x8lib.h>
#include <Wire.h>		// pas nécessaire, Arduino builtin lib !

// blibliothèques pour la carte SD en SPI
#include <SPI.h>
#include <SD.h>


// TinyGPSP++ object
// pour lire les trames NMEA, le Tx du module va sur un port série 'soft' du pro-mini
 
TinyGPSPlus gps;
static const int RXPin = 8, TXPin = 9;  // seul Rx sera utilisé...
static const uint32_t GPS_Baud = 9600;
SoftwareSerial GPS_Serial(RXPin, TXPin);

// OLED display object
// *********************
// U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
//  *** la lib U8g2 est en mode graphique ! et utilise > 135 % de la RAM du pro-mini !

//  *** donc on utilise U8X8 qui est en mode caractère... et moins gourmande
//  *** le curseur est positionné en caractères (ligne/colonne) et pas en pixels !
U8X8_SH1106_128X64_NONAME_HW_I2C u8x8 (SCL_PIN,SDA_PIN,U8X8_PIN_NONE);

// SD card object
// ***************
static const int pinSD_CS = 6;     // SD card chip select pin, default 4, can be changed
static const int pin_SS = 10;      // must be in output and stay low (for the library !?)
static const int pinActiveSD = 2;   
static const int pin_log = 3;
File logFile;
unsigned long i = 0;     // pour numéroter les enregistrements du log

/* **********************************************************************************
    il n'y a pas assez de FLASH pour afficher sur un terminal en même temps que l'on écrit sur la carte SD !!!
    - de même impossible d'afficher sur l'OLED pendant que l'on écrit sur la carte SD; manque de temps
    -- donc toutes les commande sur le Serial port sont désactivé (comentées ! )

   ********************************************************************************** */

void setup()
{
  // initialisation des 2 ports série
  // --------------------------------

  //Serial.begin(115200);        // pour un éventuel terminal sur la sortie du pro-mini (ou prise RC) via un FTDI 
  GPS_Serial.begin(GPS_Baud);    // communication avec le module GPS

  // initialisation de l'écran OLED
  // -------------------------------

  u8x8.begin();   // a appeler avant toutes autres procédures u8x8
  u8x8.clear();
  //u8x8.setFont(u8x8_font_8x13B_1x2_r);    // choix de la police, voir sur GitHub, -r = réduite n'a pas les accents
  //u8x8.setFont(u8x8_font_8x13B_1x2_f);    // B = bold,  _f full set 
  // *************************************************************************
  // en ne prenant qu'une seule police j'économise 12% de mémoire !!!!! 
  u8x8.setFont(u8x8_font_8x13_1x2_f);
  /**********************************************************************************
   *  4 lignes  de 16 charactères ... lisibles, matrice x= 0-15 et y= 0-7
   *  MAIS indices de ligne 0, 2, 4, 6 pour la font 8x13 (qui prend 2 lignes !!!)
   * ******************************************************************************** */

  //u8x8.setFont(u8x8_font_5x8_f);          // ça c'est un peu trop petit !!!
  u8x8.clearDisplay();
  u8x8.print("GPS data Logger");            // sera positionné à x = ligne = 0 et y= colonne = 0  (caractères not pixel)
  //u8x8.setFont(u8x8_font_8x13_1x2_f);     // j'utilise qu'une seule police pour économiser la mémoire !
  u8x8.drawString(0,2, "TOA_08.17.unicsv");  // variantes String pour le 'print'. Ver: 'n' pour numérotation des records et 'f' pour full records
  u8x8.drawUTF8(0,6," > GPS not ready");    // ...et pour sortir les caractères UTF8
  delay(2000);

  // initialisation du lecteur de carte SD
  // --------------------------------------

  //Serial.println("Initializing SD card...");

  // make sure that the default chip select pin is set to
  // output, even if you don't use it:
  pinMode(pin_SS, OUTPUT);
  pinMode(pinActiveSD, OUTPUT);	  // connected (with a wire bridge) to 'pin_log' to activate it
	pinMode(pin_log, INPUT_PULLUP);   /* this one stay HIGH by default !
									              	will be LOWered to Active the log...
						              				returning HIGH to close the file before releasing the SDcard !*/
	  
  while (!SD.begin(pinSD_CS))
  { 
    u8x8.drawString(0,4,"pas de carte SD!");
    //Serial.println("-insert a SD card...");
    delay(2000);
  }

  /*u8x8.clearLine(2);    // la font prend 2 lignes!
    u8x8.clearLine(3);
    u8x8.clearLine(4);
    u8x8.clearLine(5);
  */
  u8x8.drawUTF8(0,4,"-- carte insérée");      // UTF8 pour avoir les accents
  // on se laisse le temps de voir l'écran de bienvenue
  // (le gps ne sera pas prêt, de toute façon)
  delay(2000);

}

// ---------  end of setup  ---------------------------------------------------

/*  ****************************************************************************
    * la bonne procédure pour lire le contenu des trames correctement
    * sans qu'elles soint "écorchées" par le traitement:
	  * Il faut attendre qu'il n'y ai plus de réception sur le port GPS_Serial
    * *************************************************************************** */

void loop()
{
  static unsigned long last_rx_time = 0;
  static bool waiting_for_quiet_time = true;

  //if (digitalRead(pin_log) == HIGH ) logFile.close();  // HIGH: jumper open, log fini !
  // ? ligne inutile ?
  
  // Handle GPS data
  while (GPS_Serial.available() > 0)	// il y a 1 caractères ou plus, à lire dans le buffer (de 64 bytes)
  {
    if (gps.encode(GPS_Serial.read()))
    {
      // set some variables, *NO PRINTS OR SD LOGGING*
    }
    last_rx_time = millis();
  }	

  // See if the GPS device went quiet for a while (5 ms)
  // pour être sûr que le flux du GPS_serial est arrêté
  // et il devrait rester 250 ms avant la prochaine trame
  if (millis() - last_rx_time > 5UL)
  {
    // Serial.println("reading GPS_serial");               // for debug only: ok

    if (waiting_for_quiet_time && gps.location.isValid() && gps.time.isUpdated())
    {
        // il faut contrôler que l'heure ai été mise à jour (à la seconde !)
        // sinon on obtient les valeurs des trames _GSV de tous les satellites visibles (facilement 4 !)
    
        // Do these things just *once* per interval.
        // This flag gets reset when we start receiving data again.

        //u8x8.drawString(0,6,"> data available");    // inutile, si on les on les affiche !
  /*
            ************************************************************************************** 
             pour tester seulement, mémoire trop petite pour mettre la sortie Terminal et OLED !
            *************************************************************************************** */

  /*        Serial.print("Date= ");
            Serial.print(gps.date.year());
            Serial.print(".");
            Serial.print(gps.date.month());
            Serial.print(".");
            Serial.print(gps.date.day());	
            Serial.print("  ");
            Serial.print(gps.time.hour());
            Serial.print(":");
            Serial.print(gps.time.minute());
            Serial.print(":");
            Serial.println(gps.time.second());
            Serial.print("LAT = ");	Serial.println(gps.location.lat(), 4);
            Serial.print("LONG= ");	Serial.println(gps.location.lng(), 4);
            Serial.print("ALT = ");	Serial.println(gps.altitude.meters(), 0);
            Serial.print("SAT = ");	Serial.println(gps.satellites.value()); // Number of satellites in use (u32)
            Serial.print("HDOP= ");	Serial.println((gps.hdop.value()/100.0), 1); // Horizontal Dim. of Precision (100ths-i32)
            Serial.print("Speed= ");	Serial.println(gps.speed.kmph());
            Serial.print("Course= ");	Serial.println(gps.course.deg());
            Serial.print("\n");
  */
        // ********************************************************************************************* 
  
     
      waiting_for_quiet_time = false;

      // Do all the other things!
      // Log to SD
      //...use the variables you set above, although most are probably still in the GPS object...

      // **********************************************
      // affichage sur OLED et/ou log sur la carte SD
      // **********************************************

      if (gps.time.second() % 5 == 0 ) u8x8.clearDisplay();  // refresh OLED toutes les 5 ou 10 secondes seulement pour éviter le blink

      if (digitalRead(pin_log) == HIGH )           // HIGH: jumper open, no log  but full OLED display
      {
        u8x8.setCursor(0,0);                        // ligne 1 (x, Y=0)
        if (gps.date.month() < 10) u8x8.print("0");
        u8x8.print(gps.date.month());
        u8x8.print(".");
        if (gps.date.day() < 10) u8x8.print("0");
        u8x8.print(gps.date.day());
        u8x8.setCursor(8,0);
        u8x8.print(gps.time.hour());
        u8x8.print(":");
        u8x8.print(gps.time.minute());
        u8x8.print(":");
        if (gps.time.second() < 10) u8x8.print("0");
        u8x8.print(gps.time.second());

 //     if (digitalRead(pin_log) == HIGH )     // HIGH: jumper open, no log
 //     {
 //  j'avais essayer de quand même mettre l'heure pendant le log... mais ça passe pas !

        u8x8.setCursor(0,2);                // ligne 2 (x=0,y=2)
        //u8x8.print("N");                    // tu es sensé rester dans cette hémisphère !
        u8x8.print(gps.location.lat(), 4);
        u8x8.setCursor(9,2);
        //u8x8.print("E");                    // ... et à l'EST de Greenwich !
        u8x8.print(gps.location.lng(), 4);       
        u8x8.setCursor(0,4);               // ligne 3 (x,Y=4)
        //u8x8.print("S_");
        u8x8.print("S");
        if (gps.satellites.value() < 20){
          u8x8.print(gps.satellites.value());
        }
        //u8x8.print(" P_");
        u8x8.print(", P");
        u8x8.print(gps.hdop.value()/100.0, 0);
        u8x8.print(",");
        
        u8x8.setCursor(8,4);
        if (gps.speed.kmph() < 1000){
          u8x8.print(gps.speed.kmph(), 0);
        }
        u8x8.print(" km/h");

        u8x8.setCursor(0,6);               // ligne 4 (x,Y=6)
        u8x8.print(gps.altitude.meters(), 0);
        u8x8.print(" m/M");

        u8x8.setCursor(11,6);
        u8x8.print(gps.course.deg(), 0);
        u8x8.print("\xb0");             // code du symbole "°" dans la police utilisée  !
      }
      else        // jumper closed, log actif, OLED display restricted !
      {

        /* --- pas possible de mettre cet affichage si on log !!! sinon ça plante !!!
          if (gps.time.second() % 5 == 2 ) u8x8.drawString(4,4,"Wait ....");
          
          else u8x8.drawString(4,4,"Recording");
        */        //Serial.println("Write on SD");      // for debug only


        /* ************************************************************************
           * il faut que le fichier contienne au moins un CR,LF en UTF8
           * pour que ça fonctionne !!  MAIS PAS dans l'environnement Arduino ???
           * *********************************************************************** */
  
        logFile = SD.open("TRAJET.CSV", FILE_WRITE);
        if (logFile)
        {
          //Serial.println("__LOGGING");      // for debug only

          i++;
          logFile.print(i);       // allez savoir pourquoi sans ce RECORD, il n'enregistre pas les variables (gps.xxx.vvv())
          logFile.print(",");

          //logFile.print(gps.time.value());          //pas utile pour GoogleHearth
          //logFile.print(",");
          logFile.print(gps.location.lat(), 6);
          logFile.print(",");
          logFile.print(gps.location.lng(), 6);
          logFile.print(",");
          logFile.print(gps.altitude.meters());
          logFile.print(",");
          //logFile.print(gps.course.deg(), 0);        //pas utile pour GoogleHearth
          //logFile.print(",");
          logFile.println(gps.speed.mps());           // GoogleHearth veut la vitesse en m/s
          logFile.close();                      // et ne pas oublier le CR/LF avant le CLOSE!
        }
      }
    } 
  }
	else
	{
		// We're getting data again, reset the flag.
		waiting_for_quiet_time = true;
	 }
  
}     // end of loop

//  fin du fichier 'main.cpp', TOA, 08.08.2021

