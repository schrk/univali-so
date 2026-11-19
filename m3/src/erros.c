/* erros.c - Tradução dos códigos de erro em mensagens. */
#include "erros.h"

const char *fs_strerro(FsErro e)
{
    switch (e) {
    case FS_OK:               return "sucesso";
    case FS_E_NAO_EXISTE:     return "arquivo ou diretório não encontrado";
    case FS_E_JA_EXISTE:      return "arquivo já existe";
    case FS_E_PERMISSAO:      return "permissão negada";
    case FS_E_NAO_DIRETORIO:  return "não é um diretório";
    case FS_E_EH_DIRETORIO:   return "é um diretório";
    case FS_E_NAO_VAZIO:      return "diretório não vazio";
    case FS_E_NOME_INVALIDO:  return "nome inválido (use 1 a 32 caracteres: letras, dígitos, '.', '_' ou '-')";
    case FS_E_SEM_ESPACO:     return "não há espaço no disco simulado";
    case FS_E_ARQ_GRANDE:     return "arquivo grande demais (limite do inode: 1536 bytes)";
    case FS_E_SEM_MEMORIA:    return "memória insuficiente";
    case FS_E_ARGUMENTOS:     return "argumento inválido";
    case FS_E_OPERACAO:       return "operação não permitida";
    case FS_E_NAO_EXECUTAVEL: return "não é um programa executável (precisa começar com #!)";
    case FS_E_USUARIO:        return "usuário ou grupo inexistente";
    case FS_E_EM_USO:         return "diretório em uso (é o diretório atual ou um ancestral dele)";
    }
    return "erro desconhecido";
}
