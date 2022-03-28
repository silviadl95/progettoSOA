# progettoSOA

Per compilare:

nella cartella progettoSOA

make

sudo insmod driver.ko

dmesg (per conoscere il major assegnato)

sudo rmmod driver.ko (dopo l'utilizzo)

Per testare:

nella cartella progettoSOA/user

make

sudo ./user /dev/my-new-dev Major Minor

---------------------------------------------------

Implementazione:

Per la modalità non bloccante:
I thread possono scrivere fino a un massimo di 4096 byte, una volta raggiunta quella soglia le successive scritture non avvengono.
I thread che vogliono leggere un numero di byte superiore a quelli presenti nel file device leggono il massimo di byte disponibili.

Per la modalità bloccante:
Se non c'è abbastanza spazio per la scrittura il thread si mette in attesa su una waitqueue (che dipende dal minor e dalla priorità). 
L'attesa può terminare se il thread viene svegliato da un altro thread o alla scadenza di un timeout.
Se non ci sono abbastanza byte richiesti per una lettura anche quel thread si mette in attesa su una waitqueue.
Una volta svegliati controlleranno lo stato del file device per vedere se è possibile effettuare la lettura/scrittura. In caso negativo torneranno nella waitqueue.
Una volta letto/scritto i thread chiamano la funzione awake per svegliare un thread in attesa nella waitqueue.

Per le scritture asincrone di bassa priorità:
Se la scrittura è asincrona il thread crea un task che seguirà una write deferred.
Considerando che il thread che ha eseguito la scittura deve ricevere la risposta in modo sincrono dopo aver schedulato il task si posiziona nella wait queue in attesa della risposta del task.
Il task dopo aver eseguito la deferred write chiama la funzione awake e modifica un certo parametro per segnalare quanti byte ha scritto.
