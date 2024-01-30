# Sistema de Trânsito

Projeto 1 da diciplína Fundamentos de Sistemas Embarcados, desenvolvido pelo aluno Thiago Siqueira Gomes. O projeto consiste em um sistema de trânsito com servidores distribuídos para controlar cruzamentos e um sistema central para o gerenciamento remoto de um ou mais cruzamentos.

Vídeo do trabalho disponível em: <https://youtu.be/_6-BBfE5Nm4>

## Instruções de Instalação

1 - Clone o repositório com todos os arquivos para a sua máquina.
```bash
git clone https://github.com/FSE-2023-2/trabalho-1-2023-2-thgomes.git
```

2 - Copie a pasta "distributed_intersection" para as máquinas que faram o papel de Cruzamentos Distribuídos, e o "central_server" para a máquina que fara o papel de servidor central.

3 - Entre na pasta "config/central-addr.json" do Servidor Central e coloque IP e a Porta que será utilizado no servidor central.

4 - Entre na pasta "config/central-addr.json" dos Cruzamentos Distribuídos e também coloque o IP e a Porta referente ao utilizado no Servidor Central.

5 - Dentro do diretório "central_server", execute os seguintes comandos para compilar e executar o Servidor Cetral.

```bash
make
```
```bash
./central_server
```

6 - Dentro do diretório do diretório "distributed_intersection", execute os seguintes comandos para compilar e executar o Servidor Central. No lugar do campo "option_num" coloque o número do cruzamento que deseja executar (1 ou 2).

```bash
make
./
```
```bash
./distributed_intersection option_num
./
```

Após realizer o seguinte processo para rodar o Servidor Central e duas instàncias dos Cruzamentos Distribuídos voce vera no terminal do Servidor Central uma mensagem de confimação da conexão com os dois servidores distribuídos. Isso significa que a instalação e execução foi bem sucedida e o sistema está pronto para uso.


## Instruções de Usabilidade

A interface de uso do Servidor Central é simples concisa, consiste em uma série de comandos que podem ser executados a qualquer momento durante a execução, a usabilidade em típicas interfaces de linha de comando, como a do MySql CLI por exemplo.

Aqui estão os comandos disponíveis no sistema:

- `show commands`: Exibe uma lista de todos os comandos disponíveis.

- `emergency mode`: Ativa o modo de emergência, em que a via principal fica sempre aberta.

- `night mode`: Ativa o modo noturno, em que todos os semáforos piscam em amarelo.

- `default mode`: Volta para o modo padrão de funcionamento do sistema de trânsito.

- `show violations`: Exibe todas as violações de tráfego registradas.

- `show avgspeed`: Mostra a velocidade média atual das 3 vias, descartando os carros parados do cálculo.

- `show trafficflow`: Exibe informações sobre o fluxo de tráfego em carros por minuto das 3 vias.

---
