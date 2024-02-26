/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  *
  *  AlAdc is a "lib" for ADC of STM32F103 processor and later.
  *
  *  For instructions, go to https://github.com/AntonioPrevitali/STM32AlAdc
  *  Created by Antonio Previtali 11 Jan 2024.
  *  Bug fixed 26/02/2024 now work !
  *
  *  This Code/library is free software; you can redistribute it and/or
  *  modify it under the terms of the Gnu general public license version 3
  *
  *  Qui sto provando ADC 3 canali  Vrfeint PA0 PA1 a bassa velocita.
  *  ADC clock a 500 Khz ! e sampling 239.5 cicli quindi
  *  239.5 + 12.5 = 252 cicli cioè 0.504 ms ogni conversione.
  *  Con DMA e con funzionamento simile alla mia dueAdcFast
  *
  *  Questo codice è stato sviluppato e testato con STM32CubeIDE
  *  Copyright (c) 2023 STMicroelectronics.
  *  All rights reserved.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_ADC1_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

//------------------------------------------------------------------------
//------------------------- AlAdc is a "lib" for ADC ---------------------
//------------------------------------------------------------------------
// Segue praticamente una quasi libreria per interagire con i dati
// ritornati dall' ADC via DMA.
// NB è tutto codice da non usare all'interno di interrupt, non va
//    usato negli interrupt o in codice ricorsivo.
//
// AlAdc_NbrOfConversion va messo uguale al numero di conversioni definite
//                       in cubeMX vedi MX_ADC1_Init Init.NbrOfConversion
// AlAdc_SizeBuffer indica quante scansioni complete memorizzare in questa
//                  ram buffer circolare di transito.
//                  Il DMA carica qui i dati e contemporaneamente questa
//                  libreria li gestisce. indicare un numero non meno di
//                  dieci.
//
// la dimensione del buffer in ram sarà di
//       AlAdc_NbrOfConversion * AlAdc_SizeBuffer * sizeof(uint16_t)
//
//  con 7 e 100 e le impostazioni in .ioc sono 2.275 ms di storico.
#define AlAdc_NbrOfConversion 7
#define AlAdc_SizeBuffer 100

// prototype, public metod of library !
void AlAdc_Start();                           // Avvia ADC/DMA
void AlAdc_Start_Wait(uint32_t xPrel);        // in alternativa avvia ed attende che DMA esegua n conversioni.
void AlAdc_Stop(void);                        // ferma ADC/DMA
uint32_t AlAdc_GetNumData();                  // ritorna numero di misure disponibili, in coda nel buffer
void AlAdc_SkipData(uint32_t nskip);          // avanza nel buffer senza leggere le misure
uint16_t AlAdc_GetData(uint16_t *xRank);      // ritorna una misura dal buffer
uint16_t AlAdc_FindRank(uint16_t xRank);      // cerca nel buffer la misura xRank piu recente.
uint16_t AlAdc_WaitRank(uint16_t xRank);      // Attende che arrivi nel buffer la misura xRank e la ritorna

uint16_t AlAdc_FindAvgForRank(uint16_t xRank, uint16_t pSkip, uint16_t nrM); // Va nel buffer indietreggia di pSkip posizioni
                                                                       // se pSkip zero non indietreggia
                                                                       // cerca le ultime nrM misure disponibile per quel Rank
                                                                       // fa la media delle nrM misure e ritorna il valore.

uint16_t AlAdc_GetDataAtPos(uint32_t xPos, uint16_t *xRank);  // vedi codice
uint32_t AlAdc_cndtr_to_pos(uint32_t xcndtr);                 // vedi codice

uint16_t AlAdc_FindAvgForRankPos(uint32_t xPos, uint16_t xRank, uint16_t pSkip, uint16_t nrM); // come sopra ma con anche pos di partenza.

// le due seguenti aggiunte dopo aggiunte il 24/01/2024
uint32_t AlAdc_FindPosForRankPos(uint32_t xPos, uint16_t xRank, uint16_t pSkip);
uint16_t AlAdc_GetDataAtSkip(uint32_t xPos, uint16_t pSkip);

void AlAdc_AggLastDMAPos(void);  // private da non usare


// private variable of AlAdc lib !
uint16_t AlAdc_Buffer[AlAdc_NbrOfConversion * AlAdc_SizeBuffer]; // buffer DMA

uint32_t AlAdc_LastDMAPos = 0; // ultima posizione nota dove il DMA ha caricato.
uint32_t AlAdc_LastCNDTR = 0;  // non usare è solo un ottimmizzazione
uint32_t AlAdc_currCNDTR = 0;  // per determinare AlAdc_LastDMAPos piu velocemente.

uint32_t AlAdc_LastgetPos = 0;  // ultima misura letta con AlAdc_GetData


// fa partire ADC da chiamare una sola volta all'inizio.
void AlAdc_Start()
{
	// chiama la calibrazione che male non fa !
	HAL_ADCEx_Calibration_Start(&hadc1);
    // fa partire ADC via DMA.
	HAL_ADC_Start_DMA(&hadc1, (uint32_t*) AlAdc_Buffer, AlAdc_NbrOfConversion * AlAdc_SizeBuffer);
	// DMA partito attende che arrivi la prima misura.
	//     essenziale attendere che arrivi almeno la prima misura diversamente
	//     AlAdc_GetNumData ed altre potrebbero non funzionerare correttamente.
    while (hdma_adc1.Instance->CNDTR == AlAdc_NbrOfConversion * AlAdc_SizeBuffer)
	{
		  __ASM("nop");
	}
    AlAdc_LastCNDTR = AlAdc_NbrOfConversion * AlAdc_SizeBuffer - 1;
    AlAdc_LastgetPos = AlAdc_LastCNDTR;
    AlAdc_LastDMAPos = 0;

}


// questa è in alternativa alla AlAdc_Start ed oltre ad avviare
// attende che vengano caricati nel buffer un certo numero di conversioni
// Vedi AlAdc_FindRank
void AlAdc_Start_Wait(uint32_t xPrel)
{
	if (xPrel > (AlAdc_NbrOfConversion * AlAdc_SizeBuffer) -1  ) xPrel = (AlAdc_NbrOfConversion * AlAdc_SizeBuffer) - 1;
	// qui sopra cera un #BUG# mancava -1 sarebbe da aggiornare nel publicato github
	if (xPrel == 0) xPrel = 1; // la start attende comunque il primo.
	// avvia
	AlAdc_Start();
	// attende
	while (AlAdc_GetNumData() < xPrel )
	{
		  __ASM("nop");
	}
}


// volendo si puo fermare ADC e farlo ripartire in seguito
// ma in pratica anziche fermarlo conviene lasciarlo attivo
void AlAdc_Stop(void)
{
	HAL_ADC_Stop_DMA(&hadc1);
    // con stop il buffer rimane valorizzato e fortunatamente anche CNDTR
	// quindi è fattibile fermare e leggere gli ultimi dati caricati in buffer.
	// quando poi si riparte i dati in buffer diciamo vengono persi e si ricomincia
	// da capo.
}


// aggiorna la variabile AlAdc_LastDMAPos con ultima posizione nota di caricamento del DMA.
// con un ottimizzazione interna di velocità.
void AlAdc_AggLastDMAPos(void)
{
	AlAdc_currCNDTR = hdma_adc1.Instance->CNDTR;
	if (AlAdc_currCNDTR != AlAdc_LastCNDTR)
	{
  	  AlAdc_LastCNDTR = AlAdc_currCNDTR;
	  if (AlAdc_LastCNDTR == AlAdc_NbrOfConversion * AlAdc_SizeBuffer)
	  {
		AlAdc_LastDMAPos = AlAdc_NbrOfConversion * AlAdc_SizeBuffer - 1;
	  }
	  else
	  {
		AlAdc_LastDMAPos = AlAdc_NbrOfConversion * AlAdc_SizeBuffer - 1 - AlAdc_LastCNDTR;
	  }
	}
}


// questa è l'unica funzione usabile all'interno di un interrupt per
// memorizzare una posizione da usare poi in seguito fuori dall'interrupt.
// ancora piu veloce catturare Instance->CNDTR memorizzarlo
// e convertirlo poi in posizione fuori dall'interrupt.
uint32_t AlAdc_cndtr_to_pos(uint32_t xcndtr)
{
  if (xcndtr == AlAdc_NbrOfConversion * AlAdc_SizeBuffer)
	   return AlAdc_NbrOfConversion * AlAdc_SizeBuffer - 1;
  else
	   return AlAdc_NbrOfConversion * AlAdc_SizeBuffer - 1 - xcndtr;
}


// questa funzione ritorna 0 se non vi sono nuovi dati da leggere con AlAdc_GetData
//                 oppure ritorna il numero di conversioni ADC già eseguite e disponibili
//                 per la lettura con AlAdc_GetData
uint32_t AlAdc_GetNumData()
{
	// aggiorna AlAdc_LastDMAPos
	AlAdc_AggLastDMAPos();
	if (AlAdc_LastDMAPos == AlAdc_LastgetPos) return 0;
	else if (AlAdc_LastDMAPos > AlAdc_LastgetPos)
		return AlAdc_LastDMAPos - AlAdc_LastgetPos;
	else
		return (AlAdc_NbrOfConversion * AlAdc_SizeBuffer) - AlAdc_LastgetPos + AlAdc_LastDMAPos;
}


// consente di portare in avanti (buffer circolare...) il punto
// da cui vengono letti i dati con AlAdc_GetData
// Se per esempio AlAdc_GetNumData() ritorna 10
// ma di queste 10 letture si vogliono buttare vie le prime 3 piu vecchie
// e si vuole leggere quindi le 7 rimanenti piu recenti basta fare
// AlAdc_SkipData(3);  anziche chiamare 3 volte la AlAdc_GetData solo per
// avanzare nelle letture.
// Attenzione se si avanza di piu di quanto indicato da AlAdc_GetNumData
// si otterranno poi dati errati.
void AlAdc_SkipData(uint32_t nskip)
{
	AlAdc_LastgetPos += nskip;
	while (AlAdc_LastgetPos >= (AlAdc_NbrOfConversion * AlAdc_SizeBuffer) )
		AlAdc_LastgetPos -= (AlAdc_NbrOfConversion * AlAdc_SizeBuffer);
}


// chiamare per le n volte indicate dalla AlAdc_GetNumData
// non chiamare se prima non si è certi che vi siano dati da leggere.
// ritorna il valore convertito dall ADC (un valore)
// se si passa un puntatore diverso dal NULL ritorna anche
// in xRank non il channel/pin ma il Rank associato
// al channel/pin in design, in STM32CubeMX
// Si può anche evitare di usare il xRank in quanto è già
// noto che il primo restituito sarà il rank1 il secondo il rank2
// ecc ecc.
//
uint16_t AlAdc_GetData(uint16_t *xRank)
{
	AlAdc_LastgetPos++;
	if (AlAdc_LastgetPos >= (AlAdc_NbrOfConversion * AlAdc_SizeBuffer)) AlAdc_LastgetPos = 0;
	if (xRank != NULL)
	    *xRank = (AlAdc_LastgetPos % AlAdc_NbrOfConversion ) + 1;
	return AlAdc_Buffer[AlAdc_LastgetPos];
}


// avendo una posizione per esempio ottenuta con AlAdc_cndtr_to_pos(hdma_adc1.Instance->CNDTR)
// restituisce il valore presente nel buffer a quella posizione.
// è un accesso diretto al buffer potrebbe essere fatto anche accedendo  direttamente a AlAdc_Buffer
// ma meglio accedere con questa.
// xPos è la posizione in input
// ritorna il valore convertito dall ADC (un valore)
// in xRank se si passa un puntatore diverso dal NULL ritorna anche il Rank
// l'ultima posizione si puo ottenere anche con AlAdc_AggLastDMAPos
// e poi passando la AlAdc_LastDMAPos
uint16_t AlAdc_GetDataAtPos(uint32_t xPos, uint16_t *xRank)
{
	if (xPos >= (AlAdc_NbrOfConversion * AlAdc_SizeBuffer) ) return 0;  // secure!
	if (xRank != NULL)
		    *xRank = (xPos % AlAdc_NbrOfConversion ) + 1;
	return AlAdc_Buffer[xPos];
}


// cerca nel Buffer l'ultima misura disponibile per quel rank
// trova sempre e comunque pertanto puo essere utile
// aver avviato con AlAdc_Start_Wait(AlAdc_NbrOfConversion)
// trova sempre e comunque nel senso che ho restituisce il valore
// valido oppure restituisce 0 o addirittura
// un valore vecchio che era nel buffer da start/stop precedenti...
// comunque sia il valore restituito è quello piu recente disponibile
// e non può essere esageratamente vecchio se la scansione ADC è
// per esempio di 3 canali al massimo sarà vecchio di 2 letture.
uint16_t AlAdc_FindRank(uint16_t xRank)
{
	int32_t z1;  // ok sign
	AlAdc_AggLastDMAPos();
	z1 = (AlAdc_LastDMAPos % AlAdc_NbrOfConversion ) + 1;
	if (z1 == xRank)
	{
		// che fortuna centrato!
		return AlAdc_Buffer[AlAdc_LastDMAPos];
	}
	else if (z1 > xRank)
	{
		// ancora fortuna è da poco che lo ha letto !
		z1 = AlAdc_LastDMAPos - z1 + xRank;
	}
	else
	{
		// un po meno fortunato ma basta andare leggermente piu indietro...
		z1 = AlAdc_LastDMAPos + xRank - z1 - AlAdc_NbrOfConversion;
	}
	// buffer circolare... #BUG# faceva - di un negativo va fatto +
	if (z1 < 0) z1 = (AlAdc_NbrOfConversion * AlAdc_SizeBuffer) + z1;
	return AlAdc_GetDataAtPos(z1, NULL);
}


// attende che ADC esegua la conversione del Rank indicato
// e ritorna il valore.
uint16_t AlAdc_WaitRank(uint16_t xRank)
{
	uint32_t z0;
	uint32_t z1;
	uint32_t z2;
	uint8_t z3;   // 0 1 bool

	AlAdc_AggLastDMAPos();
	z0 = AlAdc_LastDMAPos;
	z1 = (z0 % AlAdc_NbrOfConversion ) + 1;
	if (z1 == xRank)
	{
		// che fortuna centrato!
		return AlAdc_Buffer[z0];
	}
	// calcola quante misure si dovra attendere
	if (z1 < xRank)
	{
		z2 = xRank - z1;  // poche misure da attendere...
	}
	else
	{
		z2 = AlAdc_NbrOfConversion - z1 + xRank;  // qualcosina in piu da attendere...
	}
	// determina la posizione dove ADC carichera il rank richiesto
    z1 = z0 + z2;
    if (z1 >= (AlAdc_NbrOfConversion * AlAdc_SizeBuffer) )
	{
		z1 -= (AlAdc_NbrOfConversion * AlAdc_SizeBuffer);
        z3 = 1; // cioè z1 fa round robbing.
	}
    else z3 = 0;
    // attende che DMA avanzi sino a z1 o piu di z1 (potrebbe anche essere piu veloce !)
	// il piu di z1 è complicato come test perche buffer circolare vedi codice.
	do
	{
		AlAdc_AggLastDMAPos();
        if (AlAdc_LastDMAPos == z1)  // caso facile e probabile !
        	return AlAdc_Buffer[z1];
        else
        {
           if (z3)
           {
              // cioè z1 fa round robbing.
        	  if (AlAdc_LastDMAPos < z0 && AlAdc_LastDMAPos > z1) return AlAdc_Buffer[z1];
           }
           else
           {
        	  // cioè z1 non fa round robbing.
        	  if (AlAdc_LastDMAPos > z1 ) return AlAdc_Buffer[z1];
              z2 = z1 - AlAdc_LastDMAPos;  // distanza attuale da obbbiettivo
              if (z2 > AlAdc_NbrOfConversion)    // distanza aumentata tanto anziche diminuire
            	       return AlAdc_Buffer[z1];  // poiche ha fatto round robbing il dma.
           }
        }
	} while (1);
}


// Va nel buffer indietreggia di pSkip posizioni
// se pSkip zero non indietreggia
// cerca le ultime nrM misure disponibile per quel Rank
// fa la media delle nrM misure e ritorna il valore.
uint16_t AlAdc_FindAvgForRank(uint16_t xRank, uint16_t pSkip, uint16_t nrM)
{
	AlAdc_AggLastDMAPos();
	return AlAdc_FindAvgForRankPos(AlAdc_LastDMAPos, xRank, pSkip, nrM);
}


// come sopra ma con anche pos di partenza.
uint16_t AlAdc_FindAvgForRankPos(uint32_t xPos, uint16_t xRank, uint16_t pSkip, uint16_t nrM)
{
	int32_t z0;  // ok sign
	int32_t z1;  // ok sign
	uint32_t sumavg;
	uint16_t xi;

	if (pSkip > 0)
	{
		z0 = xPos - pSkip;
        // #BUG# faceva - di un negativo va fatto +
		if (z0 < 0) z0 = (AlAdc_NbrOfConversion * AlAdc_SizeBuffer) + z0;  // buffer circ.
	}
	else z0 = xPos;
    // qui z0 è posizione da cui iniziare a cercare il Rank
	z1 = (z0 % AlAdc_NbrOfConversion ) + 1;
	if (z1 == xRank)
	{
		z1 = z0;
	}
	else if (z1 > xRank)
	{
		z1 = z0 - z1 + xRank;
	}
	else
	{
		z1 = z0 + xRank - z1 - AlAdc_NbrOfConversion;
	}
    // #BUG# faceva - di un negativo va fatto +
	if (z1 < 0) z1 = (AlAdc_NbrOfConversion * AlAdc_SizeBuffer) + z1;  // buffer circ.
	if (z1 >= (AlAdc_NbrOfConversion * AlAdc_SizeBuffer) ) return 0;   // secure, non succede se parametri in corretti. a causa del #BUG# succedeva !
	sumavg = AlAdc_Buffer[z1];
    // per trovare gli altri da caricare in sumavg basta indietreggiare di AlAdc_NbrOfConversion
    for (xi = 1; xi < nrM; xi++)  // parte da 1, uno già caricato in sumavg
	{
	    z1 = z1 - AlAdc_NbrOfConversion;  // indietreggia
        // #BUG# faceva - di un negativo va fatto +
	    if (z1 < 0) z1 = (AlAdc_NbrOfConversion * AlAdc_SizeBuffer) + z1; // buffer circ.
	    sumavg += AlAdc_Buffer[z1];
	}
    return sumavg / (uint32_t) nrM;
}


// 24/01/24 aggiunta dopo la prima publicazione e dopo aver risolto alcuni #BUG#
//          questa consente passando una posizione di andare nel buffer
//          a cercare il Rank, solo che non ritorna il valore bensi la posizione.
//          Per ottenere il valore usare poi la AlAdc_GetDataAtPos
//  Con la pSkip si puo indietreggiare prima di cercare
//  se pSkip zero non indietreggia
//
uint32_t AlAdc_FindPosForRankPos(uint32_t xPos, uint16_t xRank, uint16_t pSkip)
{
	int32_t z0;  // ok sign
	int32_t z1;  // ok sign

	if (pSkip > 0)
	{
		z0 = xPos - pSkip;
		if (z0 < 0) z0 = (AlAdc_NbrOfConversion * AlAdc_SizeBuffer) + z0;  // buffer circ. si + perche negativo...
	}
	else z0 = xPos;
    // qui z0 è posizione da cui iniziare a cercare il Rank
	z1 = (z0 % AlAdc_NbrOfConversion ) + 1;
	if (z1 == xRank)
	{
		z1 = z0;
	}
	else if (z1 > xRank)
	{
		z1 = z0 - z1 + xRank;
	}
	else
	{
		z1 = z0 + xRank - z1 - AlAdc_NbrOfConversion;
	}
    // si + perche negativo...
	if (z1 < 0) z1 = (AlAdc_NbrOfConversion * AlAdc_SizeBuffer) + z1;  // buffer circ.
	if (z1 >= (AlAdc_NbrOfConversion * AlAdc_SizeBuffer) ) return 0;   // secure!
    return z1;
}


// è come la AlAdc_GetDataAtPos ma non ritorna il dato alla posizione xPos ma alla pSkip
// indietro della xPos passata.
// Utile usata con la AlAdc_FindPosForRankPos
// si cerca per esempio Rank=3 e poi si AlAdc_GetDataAtSkip indietro di 1 di 2 ecc..
// per leggere gli altri Rank.
// Rispetto alla AlAdc_GetDataAtPos non c'è il Rank di ritorno.
uint16_t AlAdc_GetDataAtSkip(uint32_t xPos, uint16_t pSkip)
{
	int32_t z0;

	z0 = xPos - pSkip;
	if (z0 < 0) z0 = (AlAdc_NbrOfConversion * AlAdc_SizeBuffer) + z0;  // buffer circ. si + perche negativo...
	if (z0 < 0 || z0 >= (AlAdc_NbrOfConversion * AlAdc_SizeBuffer) ) return 0;  // secure!
	return AlAdc_Buffer[z0];
}

//------------------------------------------------------------------------
//------------------------- End of AlAdc "lib" ---------------------------
//------------------------------------------------------------------------

// segue codice di esempio la parte che segue è solo per mostrare come
// utilizzare i metodi AlAdc_
uint32_t myTest[100];     // only for test, sample inspect with debugger.
uint32_t myTcnt = 0;      // counter

uint32_t myTestNr = 1;
uint32_t myCycle = 0;

uint16_t myval1;
uint16_t myval2;
uint16_t myval3;

uint16_t myval;

uint16_t myRank;

uint32_t myNr;


void FitToMyTest(uint32_t xv)
{
	if (myTcnt < 100)
	{
		myTest[myTcnt] = xv;
		myTcnt++;
	}
}



/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC1_Init();
  /* USER CODE BEGIN 2 */

  // Attiva DMA e libreria AlAdc
  AlAdc_Start();

  // AlAdc_Start_Wait(30);  // start and wait first 30 adc conversion


  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

	if (myTestNr == 1)  // il modo piu semplice di utilizzo
	{

		// MODE 1 il modo piu semplice ---------------------------------
	    if (AlAdc_GetNumData() >= 3)  // "polling of 3 conversion ready"
	    {
	    	myval1 = AlAdc_GetData(NULL);  // get 1° adc value
	    	myval2 = AlAdc_GetData(NULL);  // get 2° adc value
	    	myval3 = AlAdc_GetData(NULL);  // get 3° adc value

	    	// ok code to process value here
	    	FitToMyTest(myval1);
	        FitToMyTest(myval2);
	    	FitToMyTest(myval3);
	    	myCycle++;
	    	if (myCycle == 2)
	    	{
	    		myCycle = 0;
	    		myTestNr = 2;   // array myTest fit with 6 row
	    	}
	    }
		// END MODE 1 ---------------- ---------------------------------

	}


	if (myTestNr == 2)  // Sample 2
	{
		// MODE 2 of usage ---------------------------------
	    if (AlAdc_GetNumData() >= 1)
	    {
	    	myval = AlAdc_GetData(&myRank);

	    	// ok code to process value here
	    	// qui myval contiene il valore convertito e myRank quale Rank

	        FitToMyTest(myRank);
	        FitToMyTest(myval);
	    	myCycle++;
	    	if (myCycle == 2)
	    	{
	    		myCycle = 0;
	    		myTestNr = 3;   // array myTest fit with +4 row
	    	}

	    }

	}

	if (myTestNr == 3)  // sample 3
	{
		// MODE 3 of usage ---------------------------------
		HAL_Delay(50); // ADC and DMA working ...

		myNr = AlAdc_GetNumData();  // quanti elementi disponibili qui dopo il delay..
		if (myNr > 2)
		{
			FitToMyTest(myNr);
			myNr--;                          // li skippa tutti tranne l'ultimo.

			   AlAdc_SkipData(myNr);         // skip myNr element
			   myval = AlAdc_GetData(&myRank);  // read l'ultimo convertito

			FitToMyTest(myRank);
			FitToMyTest(myval);

	    	myCycle++;
	    	if (myCycle == 2)
	    	{
	    		myCycle = 0;
	    		myTestNr = 4;   // array myTest fit with +6 row
	    	}
		}

	}


	if (myTestNr == 4)  // sample 4
	{

		// MODE 4 is not polling is very fast
		myval2 = AlAdc_FindRank(2);  // go in buffer and return last rank2 available.
		myval1 = AlAdc_FindRank(1);
		myval3 = AlAdc_FindRank(3);

		// ok code to process value here
    	FitToMyTest(myval1);
        FitToMyTest(myval2);
    	FitToMyTest(myval3);

    	myCycle++;
    	if (myCycle == 2)
    	{
    		myCycle = 0;
    		myTestNr = 5;   // array myTest fit with +6 row
    	}

	}


	if (myTestNr == 5)  // sample 5
	{

		// MODE 5 code locked and wait adc conversion execution
		myval1 = AlAdc_WaitRank(1);
		myval2 = AlAdc_WaitRank(2);

    	FitToMyTest(myval1);
        FitToMyTest(myval2);

    	myCycle++;
    	if (myCycle == 2)
    	{
    		myCycle = 0;
    		myTestNr = 6;   // array myTest fit with +4 row
    	}

	}


	if (myTestNr == 6)  // sample 6
	{
		myval3 = AlAdc_FindAvgForRank(3, 0, 5);   // average value of last five measure of rank 3

		// ok code to process value here
		FitToMyTest(myval3);
    	myCycle++;
    	if (myCycle == 2)
    	{
    		myCycle = 0;
    		myTestNr = 7;   // array myTest fit with +2 row
    	}

	}

	if (myTestNr == 7)
	{
		myval1 = myTest[0];   // END OF SAMPLE PUT Breakpoint here and view myTest array
		FitToMyTest(1000000); // one million dollar library !!!!
	}



  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL16;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV16;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV8;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
  hadc1.Init.ContinuousConvMode = ENABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 3;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_VREFINT;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = ADC_REGULAR_RANK_2;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_1;
  sConfig.Rank = ADC_REGULAR_RANK_3;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
/* USER CODE BEGIN MX_GPIO_Init_1 */
/* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

/* USER CODE BEGIN MX_GPIO_Init_2 */
/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
