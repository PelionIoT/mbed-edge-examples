## C-API stress tester

<span class="warnings">**Warning:** This application stress tests the protocol
translator client C-API. Do not use for production implementation.</span>

### Operation description

User can specify:
- Number of threads.
- Number of protocol translators.
- User can specify the max number of devices and minimum number of devices.
- If there are more threads than protocol translators. The different threads share the same protocol translator.


Each thread in the stress tester creates devices and destroys devices, writes values to existing devices.
All this happens at the same time in each thread. The stress tester should however protect its own shared resources.


### Running

Start edge-core:

```
$ ./edge-core
```

Start the c-api-stress-tester and give the mandatory protocol translator name option:

```
$ ./c-api-stress-tester -n c-api-stress-tester
```

On Device Management, you should see the mediated endpoints appear as new devices and
they should have the sensors as resources.

The `c-api-stress-tester` supports optional command-line parameters, for example to set the Unix Domain Socket path
of Edge Core. For help, use:

```
$ ./c-api-stress-tester --help
```
