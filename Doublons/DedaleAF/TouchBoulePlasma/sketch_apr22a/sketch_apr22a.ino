float zero_senseur; 
int PIN_ACS712 = A0;

// Obtient la valeur du senseur de courant ACS712
//
// Effectue plusieurs lecture et calcule la moyenne pour pondérer
// la valeur obtenue.
float valeurACS712( int pin ){
  int valeur;
  float moyenne = 0;
  
  int nbr_lectures = 50;
  for( int i = 0; i < nbr_lectures; i++ ){
      valeur = analogRead( pin );
      moyenne = moyenne + float(valeur);
  }
  moyenne = moyenne / float(nbr_lectures);
  return moyenne;
}

void setup(){
  pinMode(2, OUTPUT);
  // calibration du senseur  (SANS COURANT)
  zero_senseur = 500;
  
  Serial.begin( 9600 );
}

float courant; 
float courant_efficace;     
float tension_efficace = 12; // tension efficace du réseau electrique
float puissance_efficace; 
float ACS712_RAPPORT = 100; // nbr de millivolts par ampère

float puissanceMean = 0.0f;
int count = 0;
void loop(){
  float valeur_senseur = valeurACS712( PIN_ACS712 );
  // L'amplitude en courant est ici retournée en mA
  // plus confortable pour les calculs
  courant = (float)(valeur_senseur-zero_senseur)/1024*5/ACS712_RAPPORT*100000;
  // Courant efficace en mA
  courant_efficace = courant / 1.414; // divisé par racine de 2

  // Calcul de la puissance.
  //    On divise par 1000 pour transformer les mA en Ampère
  puissance_efficace = (courant_efficace * tension_efficace/1000);
  
  /*Serial.println( "zero_senseur - lecture - courant efficace (mA) - Puissance (W)" );
  Serial.print( zero_senseur );
  Serial.print( " - " );
  Serial.print( valeur_senseur );
  Serial.print( " - " );
  Serial.print( courant_efficace );
  Serial.print( "mA -:" );
  Serial.print( puissance_efficace );
  Serial.println( " W" );*/
  puissanceMean = puissanceMean*0.99 + puissance_efficace*0.01;
//  Serial.print("Variable_1:");
//  Serial.print( puissance_efficace );

count+=1;
if(count%20==0)
{
  Serial.print("Variable_2:");
  Serial.println( puissanceMean );
}
  
  if(puissanceMean < -0.075)
  {
    digitalWrite(2, HIGH);
  }
  if(puissanceMean > -0.06)
    digitalWrite(2,LOW);
  //delay( 10 ); // attendre une seconde 
}