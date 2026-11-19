---
title: Seeed Studio 1.28" Round Touch Display
description: Hardware documentation for the Seeed Studio 1.28-inch circular touch display
---

# Seeed Studio 1.28" Round Touch Display

## Overview

The Seeed Studio 1.28-inch Round Touch Display is a circular IPS LCD touchscreen module designed for compact embedded projects.

## Display Specifications

- **Resolution**: 240x240 pixels (circular display)
- **Display Type**: Circular IPS LCD
- **Interface**: SPI

## Controller ICs

### Display Driver IC: GC9A01

The **GC9A01** is the display driver IC that drives the 240x240 circular IPS LCD over an SPI interface.

**Key Features:**
- 240x240 pixel resolution
- SPI interface (3-wire or 4-wire)
- Supports circular display mode
- Built-in gamma correction
- Low power consumption

### Touch Controller IC: CST816D

The **CST816D** is the touch controller IC that handles the capacitive touch layer over an I2C interface.

**Key Features:**
- 5-point capacitive touch support
- I2C interface
- Touch interrupt pin
- Low power consumption
- Supports multi-touch gestures

## Pin Configuration

| Pin | Function | Interface |
|-----|----------|------------|
| VDD | Power (3.3V) | - |
| GND | Ground | - |
| SCK | SPI Clock | SPI |
| MOSI | SPI Data | SPI |
| CS | Chip Select | SPI |
| RD | Read (optional) | SPI |
| RST | Reset | GPIO |
| INT | Touch Interrupt | GPIO |
| SDA | Touch Data | I2C (CST816D) |
| SCL | Touch Clock | I2C (CST816D) |

## Library Support

This display is supported by:
- **SPI** library (for GC9A01 display driver)
- **Touch** library (for CST816D touch controller)

## Resources

- [Seeed Studio 1.28" Round Touch Display Product Page](https://www.seeedstudio.com/1-28-Round-Touch-Display-p-5050.html)
- [GC9A01 Datasheet](https://www.glcw.com/gc9a01-datasheet/)
- [CST816D Datasheet](https://www.secoo.com/cst816d-datasheet/)
