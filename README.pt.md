# CryptONN — Proteção de Código PHP e Sistema de Licenciamento

**Idiomas:** [English](README.md) · [Türkçe](README.tr.md) · [Deutsch](README.de.md) · [Français](README.fr.md) · [Español](README.es.md) · [Português](README.pt.md) · [Русский](README.ru.md) · [العربية](README.ar.md) · [Polski](README.pl.md) · [Nederlands](README.nl.md)

---

> **O CryptONN Loader é gratuito e não requer nenhuma chave de licença.** O licenciamento ocorre na etapa de codificação, não no loader. Instale uma vez por servidor e ele gerencia automaticamente todas as aplicações protegidas.

---

## O que é o CryptONN?

O CryptONN é uma plataforma profissional de proteção de código-fonte PHP e gerenciamento de licenças de software, desenvolvida para fornecedores independentes de software (ISVs) e equipes de desenvolvimento que distribuem aplicações PHP comercialmente. Ele transforma arquivos PHP em um formato binário cifrado, fundamentalmente resistente à engenharia reversa, descompilação e redistribuição não autorizada — preservando desempenho nativo e compatibilidade total com infraestruturas PHP padrão.

O sistema é composto por dois componentes: o **CryptONN Encoder** (aplicação desktop usada pelo desenvolvedor para proteger arquivos PHP) e o **CryptONN Loader** (este repositório — um único arquivo PHP instalado no servidor do cliente final para executar arquivos protegidos de forma transparente).

---

## Problemas Resolvidos

| Problema | Como o CryptONN resolve |
|---|---|
| **Roubo de código-fonte** | A lógica PHP é transformada em um payload binário cifrado. Mesmo com acesso total ao sistema de arquivos, o código-fonte original não pode ser reconstruído. |
| **Implantação não autorizada** | Cada arquivo protegido contém um identificador de licença embutido, validado no servidor. Arquivos copiados para servidores sem licença recusam execução. |
| **Cumprimento de termos de licença** | Períodos de teste e datas de expiração são aplicados no servidor de licenciamento. Não há verificações no lado do cliente que possam ser contornadas. |
| **Distribuição multi-cliente** | A mesma base de código pode ser licenciada para múltiplos clientes, cada um com termos, limites de uso e restrições de domínio exclusivos. |
| **Redistribuição não autorizada** | Arquivos protegidos estão vinculados a identificadores de licença específicos — são inúteis sem uma licença ativa e válida. |

---

## Como Funciona

**No momento da codificação** (máquina do desenvolvedor):
Um arquivo PHP é processado pelo CryptONN Encoder. O resultado é um arquivo binário `.cryptonn` contendo um payload cifrado e um identificador de licença embutido. O código-fonte PHP original não está presente na saída em nenhuma forma.

**Em tempo de execução** (servidor do cliente):
1. O PHP tenta executar um arquivo `.cryptonn`
2. O Loader intercepta a execução via mecanismo `auto_prepend_file` do PHP
3. O Loader lê o identificador de licença embutido no cabeçalho do arquivo
4. O Loader contata a API de licenciamento CryptONN, apresentando o identificador de licença e uma impressão digital única derivada da identidade de rede do servidor
5. A API valida a licença e retorna uma chave de descriptografia, cifrada especificamente para este servidor
6. O Loader descriptografa o payload PHP inteiramente na memória
7. O código PHP descriptografado executa nativamente — nenhum arquivo temporário contendo código-fonte é retido no disco

---

## Modelo de Segurança

| Propriedade | Detalhe |
|---|---|
| **Chaves armazenadas no servidor** | Nenhuma. Nenhuma chave criptográfica é armazenada no sistema de arquivos ou ambiente. |
| **Vinculação ao servidor** | Cada servidor tem uma impressão digital única derivada de sua identidade de rede. Um pacote de descriptografia válido para um servidor é criptograficamente inútil em qualquer outro. |
| **Comunicação com a API** | Toda entrega de chaves ocorre por canais HTTPS cifrados. As chaves são adicionalmente cifradas com uma chave de envolvimento específica do servidor antes da transmissão. |
| **Tolerância offline** | Um cache de três camadas (em processo → arquivo 24h → período de graça 72h) garante operação durante interrupções temporárias da API. |
| **Aplicação de trial** | Licenças de trial têm expiração rigorosa no servidor. Nenhum período de graça offline se aplica — a API deve confirmar a validade para licenças de trial a cada cache miss. |
| **Detecção de adulteração** | Arquivos `.cryptonn` truncados, modificados ou corrompidos são detectados e rejeitados antes de qualquer tentativa de descriptografia. |
| **Sem texto simples em disco** | O código PHP descriptografado nunca é gravado em local persistente. Arquivos de execução temporários são deletados imediatamente após o uso. |

---

## Compatibilidade PHP

| Versão PHP | Status |
|---|---|
| PHP 7.2 | ✅ Totalmente suportado |
| PHP 7.3 | ✅ Totalmente suportado |
| PHP 7.4 | ✅ Totalmente suportado |
| PHP 8.0 | ✅ Totalmente suportado |
| PHP 8.1 | ✅ Totalmente suportado |
| PHP 8.2 | ✅ Totalmente suportado |
| PHP 8.3 | ✅ Totalmente suportado |
| PHP 8.4 | ✅ Totalmente suportado |
| PHP 8.5 | ✅ Totalmente suportado |
| PHP 5.x · 7.0 · 7.1 | ❌ Não suportado |

---

## Requisitos do Sistema

| Componente | Requisito | Notas |
|---|---|---|
| PHP | 7.2 – 8.5 | Todas as versões menores suportadas |
| ext-sodium | Qualquer versão | Incluído com PHP 7.2+ — nenhuma instalação separada necessária |
| ext-openssl | Qualquer versão | Disponível por padrão em praticamente todos os ambientes de hospedagem |
| HTTPS de saída | Porta 443 | Necessário para chamadas à API de validação de licença |
| Disco (cache) | ~10 KB por licença ativa | Arquivos temporários no diretório temp do sistema |
| APCu (opcional) | Qualquer versão | Habilita cache em processo; reduz significativamente a latência de inicialização a frio |

---

## Instalação

### Passo 1 — Baixar o Loader

```bash
sudo mkdir -p /opt/cryptonn
sudo curl -fsSL https://raw.githubusercontent.com/LAICOS-LTD/cryptonn-loader/main/cryptonn-loader.php \
     -o /opt/cryptonn/cryptonn-loader.php
sudo chmod 644 /opt/cryptonn/cryptonn-loader.php
sudo chown root:root /opt/cryptonn/cryptonn-loader.php
```

### Passo 2 — Configurar o PHP (escolha seu ambiente)

**cPanel / EasyApache 4**
```bash
echo "auto_prepend_file = /opt/cryptonn/cryptonn-loader.php" \
  >> /opt/cpanel/ea-phpXX/root/etc/php.ini
/scripts/restartsrv_apache && /scripts/restartsrv_php_fpm
```

**Plesk / DirectAdmin — `.user.ini`**
```ini
auto_prepend_file = /opt/cryptonn/cryptonn-loader.php
```

**Servidor Dedicado — PHP-FPM**
```ini
php_admin_value[auto_prepend_file] = /opt/cryptonn/cryptonn-loader.php
```
```bash
systemctl restart php8.2-fpm
```

**Apache — `.htaccess`**
```apache
php_value auto_prepend_file /opt/cryptonn/cryptonn-loader.php
```

### Passo 3 — Verificar a Instalação

Salve o seguinte como `/tmp/cnn-verify.php` e execute:

```php
<?php
echo defined('_CNN_MAGIC') ? "✅ CryptONN Loader: Ativo\n" : "❌ CryptONN Loader: Não carregado\n";
echo "Versão PHP  : " . PHP_VERSION . "\n";
echo "ext-sodium  : " . (extension_loaded('sodium')  ? "✅" : "❌ AUSENTE") . "\n";
echo "ext-openssl : " . (extension_loaded('openssl') ? "✅" : "❌ AUSENTE") . "\n";
echo "APCu        : " . (function_exists('apcu_store') ? "✅ Disponível" : "— Não disponível (opcional)") . "\n";
```

```bash
php /tmp/cnn-verify.php
```

---

## Resolução de Problemas

### `CryptONN Loader requires ext-sodium`
**Causa:** A extensão `sodium` não está habilitada para a versão PHP ativa.

```bash
# cPanel / EasyApache 4
/scripts/install_ea_metapackage ea-php82-php-sodium

# AlmaLinux / RHEL / CentOS 8+
dnf install php-sodium

# Ubuntu / Debian
apt-get install php8.2-sodium

# Verificar
php -m | grep sodium
```

---

### `CryptONN Loader requires ext-openssl`
**Causa:** A extensão `openssl` não está habilitada.

Habilite `extension=openssl` no `php.ini` relevante, ou instale o pacote `php-openssl` via seu gerenciador de pacotes.

---

### `Master key could not be retrieved`
**Causa:** O Loader não consegue alcançar a API de licenciamento CryptONN. Isso pode ser causado por uma regra de firewall bloqueando HTTPS de saída, falha de resolução DNS ou problema temporário de rede.

```bash
curl -sv --max-time 10 https://api.laicos.com.tr/health
```

**Soluções:**
- Certifique-se de que a porta TCP 443 de saída está permitida no servidor
- Verifique se o servidor consegue resolver nomes DNS externos
- Se atrás de um proxy de saída, configure a variável de ambiente `CRYPTONN_API_URL`

---

### `Invalid magic bytes`
**Causa:** O arquivo não é um arquivo CryptONN válido, ou foi corrompido durante a transferência (ex.: transferido em modo texto em vez de binário).

**Solução:** Retransfira o arquivo `.cryptonn` em modo binário. Não abra ou edite o arquivo com um editor de texto.

---

### `Incomplete header`
**Causa:** O arquivo `.cryptonn` está truncado — não foi transferido completamente.

**Solução:** Retransfira o arquivo. Verifique o espaço em disco disponível na origem e no destino.

---

### `Decryption failed`
**Causa:** A chave de descriptografia retornada pela API não corresponde aos parâmetros de criptografia do arquivo.

**Solução:** Confirme com o fornecedor do software que o identificador de licença correto foi usado ao codificar o arquivo.

---

### `Temporary file could not be written`
**Causa:** O processo PHP não tem permissão de escrita no diretório temporário do sistema.

**Solução:** Certifique-se de que o usuário do servidor web tem acesso de escrita ao `sys_get_temp_dir()` (tipicamente `/tmp`). Verifique as políticas SELinux ou AppArmor em sistemas endurecidos.

---

## Desempenho

| Camada de Cache | Latência Típica | Duração |
|---|---|---|
| Em processo (APCu) | < 0,1 ms | 1 hora |
| Cache de arquivo | < 0,5 ms | 24 horas |
| Chamada de API (frio) | 50 – 200 ms | No cache miss |
| Período de graça | < 0,5 ms | Até 72 horas (não-trial) |

APCu é usado automaticamente quando disponível. Nenhuma configuração adicional é necessária.

---

## Perguntas Frequentes

**P: O Loader é gratuito?**  
R: Sim. O CryptONN Loader é gratuito e de código aberto. Não há chave de licença, assinatura ou taxa associada à sua instalação em qualquer número de servidores.

**P: Funciona com PHP OPcache?**  
R: Sim. O OPcache opera no bytecode PHP após o Loader ter descriptografado e executado o código. A interação é totalmente transparente e correta.

**P: Uma instalação do Loader pode atender múltiplas aplicações?**  
R: Sim. Uma única instalação do Loader gerencia todos os arquivos `.cryptonn` no servidor, em todas as aplicações e versões PHP que o referenciam via `auto_prepend_file`.

**P: O que acontece durante uma interrupção da API?**  
R: Licenças não-trial continuam operando normalmente por até 72 horas usando o cache de arquivo. Licenças de trial requerem uma resposta bem-sucedida da API a cada cache miss — elas não se beneficiam do período de graça.

**P: Os dados em cache representam um risco de segurança?**  
R: Não. O pacote em cache é cifrado com uma chave derivada da impressão digital única do servidor. Não pode ser descriptografado em nenhuma outra máquina e não contém a chave de descriptografia bruta em nenhuma forma utilizável.

---

## Remoção do Loader

1. Remova a diretiva `auto_prepend_file` do `php.ini`, `.user.ini` ou `.htaccess`
2. Reinicie o PHP-FPM ou Apache
3. Delete o diretório do loader:
```bash
rm -rf /opt/cryptonn
```

Isso não afeta nenhum arquivo `.cryptonn` — eles simplesmente revertem para não-executáveis até que um Loader seja reinstalado.

---

## Suporte

| Canal | Link |
|---|---|
| Documentação | [laicos.com.tr](https://laicos.com.tr) |
| Rastreamento de Problemas | [GitHub Issues](https://github.com/LAICOS-LTD/cryptonn-loader/issues) |

---

*© 2026 LAICOS Technology. CryptONN é um produto da LAICOS Technology.*
