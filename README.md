# Proyecto de Sistemas Operativos
## Proyecto 4: Copy-on-Write y MiniDB (xv6)

### Integrantes

- Miguel Ángel Alzate
- Samuel Quintero Escobar

### Curso

Sistemas Operativos

### Semestre

2026-2

---

# Descripción

**Parte A (Copy-on-Write).** `fork()` ya no copia la memoria del padre. `uvmcopy()` mapea en el hijo las mismas páginas físicas; en las que eran escribibles quita `PTE_W` y pone el bit `PTE_COW` (bit 8, uno de los bits RSW que el hardware ignora), tanto en el padre como en el hijo. Cada página física tiene un contador de referencias en `kalloc.c`, protegido por un spinlock. Cuando un proceso escribe en una página CoW se produce un *store page fault* (`scause` 15); `usertrap()` llama a `cowfault()`, que copia la página si otro proceso todavía la usa o, si el proceso es el único dueño, solo le devuelve el permiso de escritura. `copyout()` resuelve el mismo caso cuando es el kernel el que escribe en memoria de usuario (por ejemplo en `read()` o `pipe()`).

**Parte B (`cowstat()`).** Llamada al sistema 25 que llena un `struct cowstat` con las páginas libres del sistema, las páginas compartidas del proceso y los contadores `cowfaults` y `cowcopies` del proceso que llama, y lo copia al usuario con `copyout()`.

**Parte C (MiniDB).** Implementamos `minidb snapshot M`. El padre calcula el checksum y el saldo total de la tabla, limpia sus bits de acceso con `pgaccess()` y hace `fork()`. El hijo escribe `snap.db` (encabezado + 8.192 cuentas) directamente desde la tabla, sin copiarla. Mientras tanto, el padre aplica las `M` transferencias; gracias a CoW, cada página que el padre modifica se copia y el hijo sigue viendo la tabla del momento del `fork()`. Al final, el padre reporta las páginas tocadas y la actividad CoW, espera al hijo, relee el archivo y verifica el checksum y la conservación del saldo total.

---

# Archivos modificados

| Archivo | Qué se cambió y por qué |
|---|---|
| `kernel/riscv.h` | Define `PTE_COW (1L << 8)`. Usa un bit RSW de la PTE, reservado para el software, para marcar las páginas compartidas que deben copiarse al escribir. |
| `kernel/kalloc.c` | Arreglo `ref_cnt[PHYSTOP / PGSIZE]` con un contador por página física y el spinlock `ref_lock`. `kalloc()` deja el contador en 1. `kfree()` lo decrementa y solo libera la página cuando llega a 0. Nuevas funciones `krefinc()` (la usa `fork`), `krefcnt()` y `kfreepages()` (cuenta la lista libre para `cowstat`). |
| `kernel/vm.c` | `uvmcopy()` comparte las páginas en vez de copiarlas: quita `PTE_W` y pone `PTE_COW` en padre e hijo, y aumenta el contador. `cowfault()` atiende el fallo: si el contador es 1 restaura `PTE_W`; si es mayor, copia la página a una nueva, actualiza la PTE y libera su referencia a la vieja. `copyout()` llama a `cowfault()` antes de escribir en una página CoW. `uvmshared()` cuenta las páginas del proceso con más de una referencia. |
| `kernel/trap.c` | En `usertrap()`, un fallo de escritura (`scause == 15`) se intenta primero como CoW; si no lo es, sigue al manejo de páginas perezosas del Proyecto 3 (`vmfault()`). |
| `kernel/proc.h`, `kernel/proc.c` | Campos `cowfaults` y `cowcopies` en `struct proc`, en 0 al crear cada proceso (`allocproc()`). |
| `kernel/cowstat.h` | Estructura `struct cowstat` que comparten el kernel, `cowtest` y `minidb`. |
| `kernel/sysproc.c`, `kernel/syscall.c`, `kernel/syscall.h` | Llamada al sistema `cowstat` (número 25). |
| `kernel/defs.h` | Prototipos de `krefinc`, `krefcnt`, `kfreepages`, `cowfault` y `uvmshared`. |
| `kernel/memlayout.h`, `user/ulib.c` | Vienen modificados en la base del Proyecto 3 (`USYSCALL`, `ugetpid()`); se entregan completos como pide el enunciado. |
| `user/user.h`, `user/usys.pl` | Declaración y stub de `cowstat()`. |
| `user/minidb.c` | `guardar_snapshot()` y `snapshot()` (Parte C.2), más dos funciones auxiliares: `saldo_total()` y `contar_bits()`, que cuenta los bits de la máscara de `pgaccess()`. |
| `Makefile` | Se agregan `_cowtest`, `_pgtbltest` y `_minidb` a `UPROGS`. |

---

# Integración con xv6

1. Clonar xv6-riscv, hacer `git switch -c proyecto4 06aad25` y
   aplicar `git apply base_proyecto3.patch`.
2. Copiar los archivos entregados en sus rutas.
3. Copiar los archivos del docente: user/cowtest.c, user/minidb.h,
   user/pgtbltest.c y user/usertests.c.
4. Compilar con `make qemu CPUS=3`.
5. Ejecutar `cowtest`, `minidb consulta 3`, `minidb snapshot 2000`,
   `pgtbltest` y `usertests -q`.

---

# Respuestas a las preguntas

## 1. PTE antes y después de una escritura CoW

Salida de `minidb mapa` (la tabla está en `0x6000`). La primera página de la tabla en el hijo es:

| Momento | PTE | Bits (V R W X U A D COW) | Dirección física |
|---|---|---|---|
| Antes de escribir | `0x21fd11d3` | V=1 R=1 **W=0** X=0 U=1 A=1 D=1 **COW=1** | `0x87f44000` |
| Después de escribir | `0x21fb28d7` | V=1 R=1 **W=1** X=0 U=1 A=1 D=1 **COW=0** | `0x87eca000` |

Los 10 bits bajos pasan de `0x1d3` a `0x0d7`. Antes de escribir, la página es válida, legible y de usuario, pero no escribible, y tiene `PTE_COW`: es la misma página física que usa el padre (A y D vienen copiados de la PTE del padre, que ya la había usado). Al escribir `t[0] = 2`, el hardware genera un fallo de escritura porque falta `PTE_W`. `cowfault()` ve que la página tiene dos referencias, reserva una página nueva, copia los 4096 bytes y deja la PTE del hijo apuntando a la copia, con `PTE_W` y sin `PTE_COW`. Por eso cambia la dirección física. La segunda página (`0x7000`) no se escribió y sigue igual: compartida, sin W y con COW.

En el padre, la PTE de esa página no cambia: sigue apuntando a `0x87f44000`, sin `PTE_W` y con `PTE_COW`. Lo que cambia es el contador de referencias de esa página física, que baja de 2 a 1 cuando el hijo suelta su referencia. Si el padre escribe después, también tendrá un fallo CoW; como ya es el único dueño, `cowfault()` no copia: solo pone `PTE_W` y quita `PTE_COW`.

## 2. Páginas sin PTE_W y USYSCALL en fork()

`uvmcopy()` solo convierte en CoW las páginas que tienen `PTE_W`. Las que no lo tienen (el código, que es R+X, y la página de guarda de la pila, que `exec()` deja sin `PTE_U`) se comparten con los mismos permisos y se aumenta su contador, pero no se marcan con `PTE_COW`. No hace falta copiarlas nunca: como nadie puede escribir en ellas, compartirlas es seguro. Si se marcaran como CoW, una escritura en el código (que debe fallar y matar al proceso) se "resolvería" haciendo una copia escribible, y se perdería la protección. Por la misma razón, `cowfault()` rechaza las páginas que no tienen `PTE_COW` o `PTE_U`, y ese fallo sigue su camino normal hasta matar al proceso.

`USYSCALL` está en una dirección alta (`TRAPFRAME - PGSIZE`), fuera del rango `[0, sz)` que recorre `uvmcopy()`, así que `fork()` no la toca. El hijo recibe su propia página `USYSCALL` en `allocproc()`/`proc_pagetable()`, con su propio pid escrito. Si se compartiera con el padre, `ugetpid()` en el hijo leería la página del padre y devolvería el pid del padre. Además, como el usuario solo la puede leer, nunca habría un fallo de escritura que la separara. También habría un problema al liberar: cada proceso libera su página `USYSCALL` en `freeproc()`, y si fuera la misma se liberaría dos veces o quedaría en uso después de liberarla.

## 3. Carrera en el conteo de referencias

Supongamos que el contador no tiene lock y que el incremento y el decremento son "leer, sumar o restar, escribir". Una página P está compartida por el padre A y su hijo B, con `ref_cnt[P] = 2`.

1. CPU 0: A hace otro `fork()`. `krefinc(P)` lee `ref_cnt[P] = 2`.
2. CPU 1: B termina y `exit()` llama a `kfree(P)`, que también lee `2`.
3. CPU 0 escribe `3`, porque lo que leyó fue 2 + 1.
4. CPU 1 escribe `1`, porque lo que leyó fue 2 − 1.

Ahora hay dos dueños reales (A y el nuevo hijo C), pero el contador dice 1. Cuando C termine, `kfree(P)` lo deja en 0 y pone la página en la lista libre mientras A todavía la tiene mapeada. Otro proceso puede recibirla en `kalloc()` y A leería o escribiría datos ajenos.

Con el orden contrario (CPU 1 escribe 1 y después CPU 0 escribe 3) el contador queda en 3 con dos dueños: la página nunca llega a 0 y se pierde para siempre. Algo parecido pasa si A y B hacen `kfree(P)` a la vez (los dos leen 2 y escriben 1): nadie la usa, pero nunca se libera.

Con `ref_lock`, la lectura y la escritura de cada incremento o decremento ocurren como una sola operación, así que estas intercalaciones no pueden pasar.

## 4. Consistencia del snapshot

Después del `fork()`, el hijo y el padre comparten todas las páginas de la tabla en modo de solo lectura. El hijo solo lee la tabla para escribirla en `snap.db`. Cuando el padre modifica una cuenta, el fallo CoW le da al **padre** una copia nueva de esa página; la página física original no cambia y sigue mapeada en el hijo. Por eso el hijo escribe exactamente la tabla del momento del `fork()`, y el checksum del archivo (`0x6915D295`) es el de antes del fork, aunque la tabla del padre termine con `0x4AB3E209`.

`copias_cow` es 30 y no 64 porque solo se copian las páginas que el padre escribe. 255 de cada 256 transferencias ocurren entre las 1.024 cuentas más activas, que ocupan las primeras 8 páginas; el resto toca cuentas al azar de la tabla. En 2.000 transferencias se tocan 30 de las 64 páginas, y las otras 34 nunca se copian.

Si el padre esperara al hijo antes de aplicar las transferencias, al terminar el hijo cada página volvería a tener una sola referencia. El padre seguiría teniendo los 30 fallos CoW, porque sus PTE siguen sin `PTE_W` y con `PTE_COW`, pero en cada uno `cowfault()` vería el contador en 1 y solo restauraría el permiso de escritura. Resultado: `fallos_cow=30`, `copias_cow=0`.

## 5. Aceleración y planificador

Ticks de `minidb consulta K` (promedio de 3 corridas; tabla completa en el reporte):

| K | CPUS=3 | CPUS=1 |
|---|---|---|
| 1 | 26,0 | 25,7 |
| 2 | 13,7 | 25,7 |
| 3 | 14,0 | 25,7 |
| 4 | 14,0 | 26,7 |
| 8 | 13,7 | 26,3 |

El trabajo se reparte en trozos iguales entre K procesos independientes, que solo se comunican al final por pipes. El planificador de xv6 es Round Robin: cada CPU recorre la tabla de procesos y ejecuta el siguiente proceso `RUNNABLE` durante un *quantum* (hasta la interrupción del temporizador). Con `CPUS=3` hay tres CPUs buscando procesos, así que varios trabajadores corren al mismo tiempo y el tiempo baja de 26 ticks a unos 14 (1,9×).

Esperaríamos 3× con K=3. Medimos 1,9× porque la máquina donde corrimos QEMU tiene solo 2 núcleos físicos: las tres CPUs virtuales comparten dos núcleos reales, y el límite práctico es 2×, que ya se alcanza con K=2. En la evidencia de `pruebas/minidb.txt` (una sola corrida por caso) el script reporta 2,31× (30 → 13 ticks); la diferencia con nuestro promedio se debe a la variación de unos pocos ticks entre corridas.

De 3 a 8 trabajadores no mejora: el trabajo total es el mismo, pero solo hay 3 CPUs. Los 8 procesos se turnan por Round Robin en esas CPUs, y el trabajo extra de crear, planificar y esperar más procesos no reduce el tiempo total.

Con `CPUS=1` no hay aceleración: una sola CPU ejecuta los K trabajadores por turnos, uno por quantum. El trabajo total sigue siendo el mismo, así que los ticks se mantienen en 26 para cualquier K, o suben un poco por los cambios de contexto adicionales.

---

# Resultados de las pruebas

Todas las suites pasan con `make qemu CPUS=3` (salida completa en `pruebas/`):

| Suite | Resultado |
|---|---|
| `cowtest` | 11 de 11: TODAS LAS PRUEBAS PASARON |
| `minidb` | Revisión de resultados OK: consulta 1, 2, 3 y 8, snapshot 2000, snapshot 0 y mapa |
| `pgtbltest` | 11 de 11: TODAS LAS PRUEBAS PASARON |
| `usertests -q` | ALL TESTS PASSED |

`minidb snapshot 2000`: `checksum_inicial=0x6915D295`, `aplicadas=1978 rechazadas=22`, `paginas_tocadas=30 fallos_cow=30 copias_cow=30`, `archivo bytes=262160 ... coincide=si`, `checksum_actual=0x4AB3E209 total_conservado=si`, `snapshot: OK`.

---

# Decisiones de diseño

- **Bit `PTE_COW` en un bit RSW (bit 8).** El hardware ignora los bits RSW, así que se pueden usar para distinguir una página CoW de una página de solo lectura de verdad.
- **Contador de referencias en un arreglo indexado por dirección física**, con un solo spinlock global (`ref_lock`). Cada operación sobre el contador es corta (un incremento, un decremento o una lectura), así que un lock global es simple y suficiente. `kfree()` decrementa y lee el valor resultante dentro de la misma sección crítica, y solo libera la página cuando el valor llega a 0.
- **Si el contador es 1, no se copia.** `cowfault()` devuelve `PTE_W` sin reservar memoria. Esto es seguro porque, si solo este proceso tiene la página, nadie más puede aumentar su contador mientras se atiende el fallo: en xv6 un proceso tiene un único hilo, así que no puede estar haciendo `fork()` al mismo tiempo.
- **El fallo CoW se atiende antes que el fallo perezoso** en `usertrap()`. Una página CoW sí está mapeada, así que primero se intenta `cowfault()`, y solo si no es CoW se pasa a `vmfault()` del Proyecto 3.
- **`copyout()` resuelve CoW antes de escribir**, porque cuando escribe el kernel no hay fallo de página de usuario que lo avise.
- **MiniDB, snapshot:**
  - El hijo escribe el archivo directamente desde la tabla, sin buffer intermedio, para aprovechar CoW.
  - Los bits de acceso se limpian **antes** del `fork()`, para que `pgaccess()` muestre solo las páginas que tocaron las transferencias.
  - `cowstat()` se mide justo antes y después del ciclo de transferencias, para contar solo sus fallos.
  - El buffer para releer `snap.db` (262.160 bytes) se pide con `sbrk()`, porque la pila de usuario es de una página.
  - La verificación también comprueba que el archivo no tenga bytes de más y que el hijo haya terminado con estado 0.

---

# Limitaciones conocidas

- `sharedpages` cuenta las páginas compartidas **del espacio de direcciones del proceso que llama**, no todas las del sistema.
- `kfreepages()` recorre la lista libre completa con el lock tomado: es O(n) en páginas libres. Es aceptable para estadísticas, pero no para una llamada frecuente.
- El lock único del contador serializa todos los `fork()`, `kfree()` y fallos CoW de todas las CPUs. Con muchas CPUs, un lock por grupo de páginas reduciría la contención.
- `copias_cow` del snapshot depende de la planificación: si el hijo terminara de escribir antes de que el padre modifique una página, ese fallo no copiaría. En la práctica el hijo tarda mucho más, porque escribe 256 KB a disco, y siempre medimos `copias_cow=30`.
- Las tablas de páginas no son CoW (está fuera del alcance), así que cada `fork()` sigue reservando las páginas de la tabla de páginas del hijo.
- Los tiempos de los experimentos se midieron en un equipo de 2 núcleos físicos, lo que limita la aceleración con `CPUS=3`.

---

# Declaración de uso de IA

[Completar con las herramientas que usó cada integrante y para qué. Ejemplo: "Usamos Claude (Anthropic) como apoyo para implementar y revisar el comando snapshot de MiniDB, redactar el borrador del README y del reporte, y preparar la sustentación. Revisamos, probamos y entendemos todo el código y el texto entregado."]

Los integrantes asumen plena responsabilidad académica sobre el contenido entregado.
