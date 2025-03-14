## The original `mntr` is from [slw287r](https://github.com/slw287r/mntr.git), I dropped the `cairo` library and implement `echarts` html output version, the command line arguments usage is the same as the original `mntr`

# Monitor process and its supprocesses' MEM and CPU Usage

* MEM: RSS and SHR in g
* CPU: Total in %

## Requirements
- Linux OS
- procps

## Usage
- by process name

```
mntr <cmd_name>
```

- by process pid
```
mntr <process_pid>
```

- by run a program
```
mntr <cmd> <args> ...
```