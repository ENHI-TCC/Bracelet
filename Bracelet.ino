/////////////////////////////////////////////////
//Autors:   Victorai Chemin & Wallace Bescrovaine
//Descricao: Firmware responsável por controlar a pulseira eletrônica do projeto ENHI
/////////////////////////////////////////////////

#include <FS.h>          //Esta precisa ser a primeira referência, ou nada dará certo e sua vida será arruinada. kkk
#include <ESP8266WiFi.h> //https://github.com/esp8266/Arduino
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h> //https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h> //https://github.com/bblanchon/ArduinoJson
#include <PubSubClient.h>
#include <EEPROM.h>
#include <SPI.h>

// #define DEBUG //Se descomentar esta linha vai habilitar a 'impressão' na porta serial

#define LF char(10) // Defini quebra de linha LF (line feed)

//Coloque os valores padrões aqui, porém na interface eles poderão ser substituídos.
#define servidor_mqtt "192.168.100.33"    //URL do servidor MQTT
#define servidor_mqtt_porta "1883"        //Porta do servidor (a mesma deve ser informada na variável abaixo)
#define servidor_mqtt_usuario "tccUser"   //Usuário
#define servidor_mqtt_senha "tccPassword" //Senha

#define mqtt_topico_sub "bracelet/cry" //Tópico para subscrever o comando a ser dado no pino declarado abaixo

//Declaração do pino que será utilizado e a memória alocada para armazenar o status deste pino na EEPROM
#define pino 12  //Pino que executara a acao dado no topico "led"
#define pino2 16 //Pino que executara a acao dado no topico "motor"
#define pino3 14 //Pino que executara a acao dado no topico "btn"

#define memoria_alocada 4 //Define o quanto sera alocado na EEPROM (valores entre 4 e 4096 bytes)

WiFiClient espClient;           //Instância do WiFiClient
PubSubClient client(espClient); //Passando a instância do WiFiClient para a instância do PubSubClient

uint8_t statusAnt = 0;      //Variável que armazenará o status do pino que foi gravado anteriormente na EEPROM
bool precisaSalvar = false; //Flag para salvar os dados

int verifyStat = 0; // Declaração de variavel de status do BTN

//Função para imprimir na porta serial
void imprimirSerial(String mensagem)
{
#ifdef DEBUG
  Serial.print(mensagem);
#endif
}

//Função de retorno para notificar sobre a necessidade de salvar as configurações
void precisaSalvarCallback()
{
  imprimirSerial("As configuracoes tem que ser salvas.\n");
  precisaSalvar = true;
}

//Função que reconecta ao servidor MQTT
void reconectar()
{
  //Repete até conectar
  while (!client.connected())
  {
    imprimirSerial("Tentando conectar ao servidor MQTT...");

    //Tentativa de conectar. Se o MQTT precisa de autenticação, será chamada a função com autenticação, caso contrário, chama a sem autenticação.
    bool conectado = strlen(servidor_mqtt_usuario) > 0 ? client.connect("ESP8266Client", servidor_mqtt_usuario, servidor_mqtt_senha) : client.connect("ESP8266Client");

    if (conectado)
    {
      imprimirSerial("Conectado!\n");
      //Subscreve para monitorar os comandos recebidos
      client.subscribe(mqtt_topico_sub, 1); //QoS 1
    }
    else
    {
      imprimirSerial("Falhou ao tentar conectar. Codigo: ");
      imprimirSerial(String(client.state()).c_str());
      imprimirSerial(" tentando novamente em 5 segundos\n");
      //Aguarda 5 segundos para tentar novamente
      delay(5000);
    }
  }
}

//Função que verifica qual foi o último status do pino antes do ESP ser desligado
void lerStatusAnteriorPino()
{
  EEPROM.begin(memoria_alocada); //Aloca o espaco definido na memoria
  statusAnt = EEPROM.read(0);    //Le o valor armazenado na EEPROM e passa para a variável "statusAnt"
  if (statusAnt > 1)
  {
    statusAnt = 0; //Provavelmente é a primeira leitura da EEPROM, passando o valor padrão para o pino.
    EEPROM.write(0, statusAnt);
  }
  digitalWrite(pino, statusAnt);
  EEPROM.end();
}

//Função que grava status do pino na EEPROM
void gravarStatusPino(uint8_t statusPino)
{
  EEPROM.begin(memoria_alocada);
  EEPROM.write(0, statusPino);
  EEPROM.end();
}

void cancelaAlerta()
{
  if (client.publish("bracelet/cry", "desliga", true))
    imprimirSerial("Colocando o pino em stado BAIXO...\n");
  delay(500);
}

//Função que será chamada ao receber mensagem do servidor MQTT
void retorno(char *topico, byte *mensagem, unsigned int tamanho)
{
  //Convertendo a mensagem recebida para string
  mensagem[tamanho] = '\0';
  String strMensagem = String((char *)mensagem);
  strMensagem.toLowerCase();
  //float f = s.toFloat();

  imprimirSerial("Mensagem recebida! Topico: ");
  imprimirSerial(topico);
  imprimirSerial(". Tamanho: ");
  imprimirSerial(String(tamanho).c_str());
  imprimirSerial(". Mensagem: ");
  imprimirSerial(strMensagem + "\n");

  //Executando o comando solicitado
  imprimirSerial("Status do pino antes de processar o comando: ");
  imprimirSerial(String(digitalRead(pino)).c_str() + LF);

  if (strMensagem == "liga")
  {
    imprimirSerial("Colocando o pino em stado ALTO...\n");
    digitalWrite(pino, HIGH);
    digitalWrite(pino2, HIGH);
    gravarStatusPino(HIGH);
  }
  else if (strMensagem == "desliga")
  {
    imprimirSerial("Colocando o pino em stado BAIXO...\n");
    digitalWrite(pino, LOW);
    digitalWrite(pino2, LOW);
    gravarStatusPino(LOW);
  }
  else
  {
    imprimirSerial("Trocando o estado do pino...\n");
    digitalWrite(pino, !digitalRead(pino));
    digitalWrite(pino2, !digitalRead(pino2));
    gravarStatusPino(digitalRead(pino));
  }

  imprimirSerial("Status do pino depois de processar o comando: ");
  imprimirSerial(String(digitalRead(pino)).c_str() + LF);
}

//Função inicial (será executado SOMENTE quando ligar o ESP)
void setup()
{
#ifdef DEBUG
  Serial.begin(115200);
#endif
  imprimirSerial("...\n");
  //Fazendo o pino ser de saída, pois ele irá "controlar" algo.
  pinMode(pino, OUTPUT);
  pinMode(pino2, OUTPUT);
  pinMode(pino3, INPUT);

  //Formatando a memória interna
  //(descomente a linha abaixo enquanto estiver testando e comente ou apague quando estiver pronto)

  // SPIFFS.format();

  //Iniciando o SPIFSS (SPI Flash File System)
  imprimirSerial("Iniciando o SPIFSS (SPI Flash File System)\n");
  if (SPIFFS.begin())
  {
    imprimirSerial("Sistema de arquivos SPIFSS montado!\n");
    if (SPIFFS.exists("/config.json"))
    {
      //Arquivo de configuração existe e será lido.
      imprimirSerial("Abrindo o arquivo de configuracao...\n");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile)
      {
        imprimirSerial("Arquivo de configuracao aberto.\n");
        size_t size = configFile.size();

        //Alocando um buffer para armazenar o conteúdo do arquivo.
        std::unique_ptr<char[]> buf(new char[size]);
        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject &json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success())
        {
          //Copiando as variáveis salvas previamente no aquivo json para a memória do ESP.
          imprimirSerial("arquivo json analisado.\n");
          strcpy(servidor_mqtt, json["servidor_mqtt"]);
          strcpy(servidor_mqtt_porta, json["servidor_mqtt_porta"]);
          strcpy(servidor_mqtt_usuario, json["servidor_mqtt_usuario"]);
          strcpy(servidor_mqtt_senha, json["servidor_mqtt_senha"]);
          strcpy(mqtt_topico_sub, json["mqtt_topico_sub"]);
        }
        else
        {
          imprimirSerial("Falha ao ler as configuracoes do arquivo json.\n");
        }
      }
    }
  }
  else
  {
    imprimirSerial("Falha ao montar o sistema de arquivos SPIFSS.\n");
  }
  //Fim da leitura do sistema de arquivos SPIFSS

  //Parâmetros extras para configuração
  //Depois de conectar, parameter.getValue() vai pegar o valor configurado.
  //Os campos do WiFiManagerParameter são: id do parâmetro, nome, valor padrão, comprimento
  WiFiManagerParameter custom_mqtt_server("server", "Servidor MQTT", servidor_mqtt, 40);
  WiFiManagerParameter custom_mqtt_port("port", "Porta", servidor_mqtt_porta, 6);
  WiFiManagerParameter custom_mqtt_user("user", "Usuario", servidor_mqtt_usuario, 20);
  WiFiManagerParameter custom_mqtt_pass("pass", "Senha", servidor_mqtt_senha, 20);
  WiFiManagerParameter custom_mqtt_topic_sub("topic_sub", "Topico para subscrever", mqtt_topico_sub, 30);

  //Inicialização do WiFiManager. Uma vez iniciado não é necessário mantê-lo em memória.
  WiFiManager wifiManager;

  //Definindo a função que informará a necessidade de salvar as configurações
  wifiManager.setSaveConfigCallback(precisaSalvarCallback);

  //Adicionando os parâmetros para conectar ao servidor MQTT
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_pass);
  wifiManager.addParameter(&custom_mqtt_topic_sub);

  //Busca o ID e senha da rede wifi e tenta conectar.
  //Caso não consiga conectar ou não exista ID e senha,
  //cria um access point com o nome "AutoConnectAP" e a senha "senha123"
  //E entra em loop aguardando a configuração de uma rede WiFi válida.
  if (!wifiManager.autoConnect("AutoConnectAP", "senha123"))
  {
    imprimirSerial("Falha ao conectar. Excedeu o tempo limite para conexao.\n");
    delay(3000);
    //Reinicia o ESP e tenta novamente ou entra em sono profundo (DeepSleep)
    ESP.reset();
    delay(5000);
  }

  //Se chegou até aqui é porque conectou na WiFi!
  imprimirSerial("Conectado!! :)\n");

  //Lendo os parâmetros atualizados
  strcpy(servidor_mqtt, custom_mqtt_server.getValue());
  strcpy(servidor_mqtt_porta, custom_mqtt_port.getValue());
  strcpy(servidor_mqtt_usuario, custom_mqtt_user.getValue());
  strcpy(servidor_mqtt_senha, custom_mqtt_pass.getValue());
  strcpy(mqtt_topico_sub, custom_mqtt_topic_sub.getValue());

  //Salvando os parâmetros informados na tela web do WiFiManager
  if (precisaSalvar)
  {
    imprimirSerial("Salvando as configuracoes\n");
    DynamicJsonBuffer jsonBuffer;
    JsonObject &json = jsonBuffer.createObject();
    json["servidor_mqtt"] = servidor_mqtt;
    json["servidor_mqtt_porta"] = servidor_mqtt_porta;
    json["servidor_mqtt_usuario"] = servidor_mqtt_usuario;
    json["servidor_mqtt_senha"] = servidor_mqtt_senha;
    json["mqtt_topico_sub"] = mqtt_topico_sub;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile)
    {
      imprimirSerial("Houve uma falha ao abrir o arquivo de configuracao para incluir/alterar as configuracoes.\n");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
  }

  imprimirSerial("IP: ");
  imprimirSerial(WiFi.localIP().toString() + "\n");

  //Informando ao client do PubSub a url do servidor e a porta.
  int portaInt = atoi(servidor_mqtt_porta);
  client.setServer(servidor_mqtt, portaInt);
  client.setCallback(retorno);

  //Obtendo o status do pino antes do ESP ser desligado
  lerStatusAnteriorPino();
}

//Função de repetição (será executado INFINITAMENTE até o ESP ser desligado)
void loop()
{
  verifyStat = digitalRead(pino3);
  if (verifyStat == HIGH)
  {
    cancelaAlerta();
  }
  if (!client.connected())
  {
    reconectar();
  }
  if (client.loop())
  {
    reconectar();
  }
}