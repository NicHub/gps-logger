/**************************************************************
  Projet: GPS_Logger, AT août 2021  Version: ToA_9.6Wd.unicsv

  _9.6: pour "9600 Bds" de la sortie série du U-Blox
  w:    pour l'utilisation d'un module "WEMOS"
  d:    pour des records dépendants d'une "distance" (10 m)
  unicvs: pour le format d'enregistrement du fichier "TRAJET.CSV"

  Affichage des données du module GPS sur un écan OLED
  et/ou enregistrement sur une carte SD au format 'unicsv'
  la 1ère ligne du fichier Trajet.csv décrit les champs enregistrés:
  desc, utc_t, lat, lon, ele, distance, speed    (élévation & distance en mètre, speed en m/s)
  ET doit être présente sur la carte SD que l'on utilise !

  Proc: WEMOS D1 lite car le pro-mini n'a pas assez de RAM !
                            il plante si plus que 88% de RAM
  -------------------------------------------------------------------------------------------------
  les datas utiles se trouvent toutes (même plus) dans les 2 trames GGA et RMC,
  donc je ne prend que celles-là et j'élimine toutes les autres pour gagner du temps de traitement.

 Trame GGA:	acquistion time (UTC)
		Latitude
		Longitude
		Validation du FIX (0/1)
		Nbre de satellites en poursuite
		HDOP
		Altitude (MSL)
		Correction altitude géoïde versus ellipsoïde
		2 champs pr le DGPS
		checksum
		
 Trame RMC:	heure du FIX (UTC)
		alerte logicielle (A=ok, V= warning)
		Latitude
		Longitude
		ground speed
		cap vrai
		date du FIX
		Déclinaison magnétique
		checksum
 ---------------------------------------------------------------------------------------------------

 - modifs nov 2021, v9.6Wd
 
 - reconfiguré le module U_Blox pour n'avoir que les 2 trames GGA et RMC à 9600 bds
 	-- temps du transfert série ~80 ms
   -- ceci nous laisse environ 850 ms pour calculer le déplacement, enregistrer et afficher
      il y aura un record que si le déplacement 'distance' est > que 10 m (ou autre)
 - rajoué le champ "Time" en première position du record et adapté l'entête de TRAJET.CSV)
 
****************************************************************************************** */

/*  ***************** WEMOS D1-lite pins usage *********************************

 **** ARDUINO use the "D" numbering for the ESP8266 pins ( GPIO 0 - GPIO 16 )!

    A0: analog input max 3.2 volts

 #define D0 	16	// déclarée Tx du softSerial, car pas utilisée  & doit être HIGH au boot
 #define D1	  5    // I2C: SCL
 #define D2	  4    // I2C: SDA
 #define D3	  0    // 10k pull-up: déclarée lecture du switch LOG
 #define D4	  2		// 10k pull-up, with BuiltIn-LED: déclarée Rx du softSerial
 #define D5	  14    // SPI: SCK
 #define D6	  12    // SPI: MISO
 #define D7	  13    // SPI: MOSI
 #define D8	  15    // SPI: SS or CS (= Slave_Select or Chip_Select), 10k pull-down, must be low at boot
 #define D9	  3		// used for Rx UART USB
 #define D10  1	  // used for Tx UART USB
*/


// bibliothèque pour le module GPS Ublox NEO-6

#include <SoftwareSerial.h> // pour avoir un 2ème port série pour le GPS
#include <TinyGPS++.h>	// version 1.0.2b, lien:  https://github.com/mikalhart/TinyGPSPlus
// mais la lib s'appelle "TinyGPSPlus" !!!

// blibliothèques pour la carte SD en SPI

#include <SPI.h>
#include <SdFat.h>    // la lib SD d'Adafruit/Arduino n'est pas compatible ESP !

// bibliothèques pour l'écran OLED en I2C

//  voir sur le lien pour la config et les fonts, c'est pas triste...
#include <U8g2lib.h>	// version "unique"?, lien: https://github.com/olikraus/u8g2
//#include <U8x8lib.h>	// elle est inclue dans la précédente !
#include <Wire.h>		// pas nécessaire, Arduino builtin lib !

// TinyGPSP++ object
// pour lire les trames NMEA, le Tx du module va sur un port série 'soft' du processeur
 
TinyGPSPlus gps;
static const int RXPin = D4, TXPin = D0;  // seul Rx est utilisé... 
// ***** et D0 doit être  HIGH au boot
static const uint32_t GPS_Baud = 9600;
SoftwareSerial GPS_Serial(RXPin, TXPin);

// OLED display object
// *********************
// U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
//  *** la lib U8g2 est en mode graphique ! et utilise > 135 % de la RAM du pro-mini !

//  *** donc on utilise U8X8 qui est en mode caractère... et moins gourmande
//  *** le curseur est positionné en caractères (ligne/colonne) et pas en pixels !
//U8X8_SH1106_128X64_NONAME_HW_I2C u8x8 (SCL_PIN,SDA_PIN,U8X8_PIN_NONE);
U8X8_SH1106_128X64_NONAME_HW_I2C u8x8 (U8X8_PIN_NONE);

// SD card object
// ***************
SdFat SD;
SdFile logFile;
static const int pinSD_CS = D8;     	// SD card chip select pin, default 4, can be changed
//static const int pin_SS = 10;      	// must be in output and stay low (for the SD library only!)   
static const int pin_log = D3;   	// mise au GND pour activer le log
unsigned long i = 0;     		// pour numéroter les enregistrements du log


//  **** subroutines  ***
//  *********************

int tic = 0;  // pour la routine 'sablier'

void sablier()
{
  tic++;
  switch (tic)
  {
    case 1:
      u8x8.drawString(14,4, "-");
      break;
    case 2:
      u8x8.drawString(14,4, "\x5C");    // car '\'
      break;
    case 3:
      u8x8.drawString(14,4, "\x7C");    // car ¦
      break;
    case 4:
      u8x8.drawString(14,4, "/");
      tic = 0;
      break;
  }
}
// ***************************************

void setup()
{
  // initialisation des 2 ports série
  // --------------------------------

  Serial.begin(115200);        		// pour un  terminal sur la sortie du processeur
  GPS_Serial.begin(GPS_Baud);    	// communication avec le module GPS

  // initialisation de l'écran OLED
  // -------------------------------

  u8x8.begin();   // a appeler avant toutes autres procédures u8x8
  u8x8.clear();
  //u8x8.setFont(u8x8_font_8x13B_1x2_r);    // choix de la police, voir sur GitHub, -r = réduite n'a pas les accents
  //u8x8.setFont(u8x8_font_8x13B_1x2_f);    // B = bold,  _f full set
   //u8x8.setFont(u8x8_font_5x8_f);          // ça c'est un peu trop petit !!!
  // *************************************************************************
  // en ne prenant qu'une seule police j'économise de la mémoire !!!!! 
 
  u8x8.setFont(u8x8_font_8x13_1x2_f);
  /**********************************************************************************
   *  4 lignes  de 16 charactères ... lisibles, matrice x= 0-15 et y= 0-7
   *  MAIS indices de ligne 0, 2, 4, 6 pour la font 8x13 (qui prend 2 lignes !!!)
   * ******************************************************************************** */
  
  u8x8.clearDisplay();
  u8x8.print("GPS data Logger");            // sera positionné à x = ligne = 0 et y= colonne = 0  (caractères not pixel)
  //u8x8.setFont(u8x8_font_8x13_1x2_f);     // j'utilise qu'une seule police pour économiser la mémoire !
  // u8x8.drawString(0,2, "TOA_08.17.unicsv");  // variantes String pour le 'print'. Ver: 'n' pour numérotation des records et 'f' pour full records
  u8x8.drawString(0,2, "ToA_9.6Wd.unicsv");
  u8x8.drawUTF8(0,6," > GPS not ready");    // ...et pour sortir les caractères UTF8
  delay(2000);

  // initialisation du lecteur de carte SD
  // --------------------------------------

  Serial.println("Initializing SD card...");

  // make sure that the default chip select pin is set to output,
  // even if you don't use it: (avec la lib SD Adafruit pour Arduino only !)
  //pinMode(pin_SS, OUTPUT);
 
  pinMode(pin_log, INPUT_PULLUP);		/* this one stay HIGH by default !
						will be LOWered to Active the log...
						returning HIGH to close the file before releasing the SDcard !*/
	  
  while (!SD.begin(pinSD_CS))
  { 
    u8x8.drawString(0,4,"-- no SD card");
    sablier();
    delay(50);
  }

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
    * et comme on n'a que 2 trames, il reste ~850 ms pour faire le job
    * *************************************************************************** */

void loop()
{
  static unsigned long last_rx_time = 0;
  static bool waiting_for_quiet_time = true;

  static double last_lat = 0;    // position fictive de démarrage,
  static double last_lng = 0;    // sera updatée chaque seconde par la position courrante !
  static double distance = 0;
  
  // Handle GPS data
  
  while (GPS_Serial.available() > 0)
  {
    if (gps.encode(GPS_Serial.read()))
    {
      // set some variables, *NO PRINTS OR SD LOGGING*
    }
    last_rx_time = millis();
  }	

  // See if the GPS device went quiet for a while (5 ms)
  // pour être sûr que le flux du GPS_serial est arrêté
  if (millis() - last_rx_time > 5UL)
  {
    // Serial.println("reading GPS_serial");               // for debug only: ok

    if (waiting_for_quiet_time && gps.location.isValid() && gps.time.isUpdated())
    {
        // il faut contrôler que l'heure ai été mise à jour (à la seconde !)
        // sinon on obtient les valeurs des trames _GSV de tous les satellites visibles (facilement 4 !)
    
        // Do these things just *once* per interval.
        // This flag gets reset when we start receiving data again.

	//u8x8.drawString(0,6,"> data available");    // inutile, si on les a, on les affiche !
	
	/* ***********************************
            for DEBUG only
	 ************************************* */

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
      
      Serial.println("=====Début affichage");   // for debug only
      
      if (gps.time.second() % 5 == 0 ) u8x8.clearDisplay();  	// refresh OLED toutes les 5 ou 10 secondes seulement pour éviter le blink
      if (digitalRead(pin_log) == HIGH )           		// HIGH: jumper open, no log  but full OLED display
      {
        u8x8.setCursor(0,0);                        	// ligne 1 (x, Y=0)
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

        //  j'avais essayé de quand même mettre l'heure pendant le log... mais ça passe pasavec le pro mini!

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
        
        Serial.println("=====fin_aff");   // for debug only: t_affichage_OLED = 60 à 100 ms (si clearDisplay)
      }
      else        // jumper closed, log actif, OLED display restricted !
      {
        Serial.println("______début record__");   // for debug only

        u8x8.drawString(4,4,"Recording");
        sablier();

        // calcul du déplacement: >>>>>> il y a 5250 km depuis la maison jusqu'au point   0° N / 0° E
        distance = (unsigned long) TinyGPSPlus:: distanceBetween (gps.location.lat(), gps.location.lng(), last_lat, last_lng ); 

        // Serial.print("dist: ");      // for debug only
        // Serial.println(distance);
        // Serial.print("life_lat. ");
        // Serial.println(gps.location.lat(), 6);
        // Serial.print("last_lat: ");
        // Serial.println(last_lat, 6);

        if (distance > 10.0)  //  on met à jour la dernière position et on log le record sur la SD
        {
          last_lat = gps.location.lat();
          last_lng = gps.location.lng();

          /* ************************************************************************
          * il faut que le fichier contienne au moins un CR,LF en UTF8
          * pour que ça fonctionne !!  MAIS PAS dans l'environnement Arduino ???
          * *********************************************************************** */
    
          logFile.open("TRAJET.CSV", FILE_WRITE);
        
          Serial.println("__LOGGING");      // for debug only

          i++;
          logFile.print(i);       // allez savoir pourquoi sans ce RECORD, il n'enregistre pas les variables (gps.xxx.vvv())
          logFile.print(",");
          // Serial.println(i);   // for debug only

          //logFile.print(gps.time.value());  // non formaté     // pas utile pour GoogleHearth mais pour Gérard !!!
          logFile.print(gps.time.hour());
          logFile.print(":");
          logFile.print(gps.time.minute());
          logFile.print(":");
          //if (gps.time.second() < 10) u8x8.print("0");
          logFile.print(gps.time.second());
          logFile.print(",");

          logFile.print(gps.location.lat(), 6);
          logFile.print(",");
          logFile.print(gps.location.lng(), 6);
          logFile.print(",");
          logFile.print(gps.altitude.meters());
          logFile.print(",");
          logFile.print(distance, 2);     		//pas utile pour GoogleHearth
          logFile.print(",");
          //logFile.print(gps.course.deg(), 0);    	//pas utile pour GoogleHearth
          //logFile.print(",");
          logFile.println(gps.speed.mps());        	// GoogleHearth veut la vitesse en m/s
          logFile.close();                   		// et ne pas oublier le CR/LF avant le CLOSE!
          Serial.println("___________File_closed");      // for debug only: t_record = 25 ms
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

