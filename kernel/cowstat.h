// Estadísticas de Copy-on-Write que devuelve la llamada al sistema
// cowstat() (Parte B). La usan cowtest y minidb tal cual.
struct cowstat {
  uint64 freepages;   // páginas físicas libres en todo el sistema
  uint64 sharedpages; // páginas físicas con más de una referencia
  uint64 cowfaults;   // fallos CoW atendidos para el proceso que llama
  uint64 cowcopies;   // de esos fallos, cuántos copiaron la página
};
