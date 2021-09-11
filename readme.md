# poemgr

poemgr is a utility designed to control PoE capable power delivery found on OpenWrt enabled devices.

It's designed in a modular way, abstracting PoE capabilities into profiles, which control one (or more) PSE chips.

![structural diagram](docs/images/poemgr.drawio.svg)


## Installation

Upon installation, a `poemgr` service as well as the identically named UCI package is installed. Upon installation, a configuration
matching the board `poemgr` was installed on is created.

By default, all PoE functionality is disabled.


## Usage

Currently the following commands are implemented.

### poemgr enable

Enables the profile. This command will enable the PoE functionality of the selected profile. This does not necessarily mean
PoE outputs will be enabled. It turns on PSE chips and other related hardware so `poemgr` commands can be sent to such devices.

### poemgr disable

Enables the profile. This command will enable the PoE functionality of the selected profile. This does not necessarily mean
PoE outputs will be disabled. 

### poemgr apply

Apply configuration specified using UCI. This can have impact on the PoE output power configuration.

### poemgr show

Displays information about the current state of PoE outputs as well as PSE chips.

This command does not modify the state of PoE functionality.

```
{
  "profile":"usw-flex",
  "input":{
    "type":"802.3af"
  },
  "output":{
    "power_budget":8,
    "ports":{
      "0":{
        "enabled":true,
        "active":false,
        "poe_class":-1,
        "power":0,
        "power_limit":20,
        "name":"lan5",
        "faults":[
          "open-circuit"
        ]
      },
      "1":{
        "enabled":true,
        "active":false,
        "poe_class":-1,
        "power":0,
        "power_limit":20,
        "name":"lan4",
        "faults":[
          "open-circuit"
        ]
      },
      "2":{
        "enabled":true,
        "active":false,
        "poe_class":-1,
        "power":0,
        "power_limit":20,
        "name":"lan3",
        "faults":[
          "open-circuit"
        ]
      },
      "3":{
        "enabled":true,
        "active":true,
        "poe_class":0,
        "power":2,
        "power_limit":20,
        "name":"lan2",
        "faults":[
        ]
      }
    }
  },
  "pse":[
    {
      "model":"PD69104",
      "temperature":50
    }
  ]
}
```
