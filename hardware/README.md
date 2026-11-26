# Hardware design files

The Life Logger's hardware is released under the CERN Open Hardware Licence Version 2 – Strongly Reciprocal
(CERN-OHL-S-2.0). These are its source files, the ones to edit:

- `../solidworks/`: the machined aluminum case (SolidWorks parts `box_lid.SLDPRT` and `box_bottom.SLDPRT`,
  and a drawing), with rendered views.
- `Life_Logger_BOM.csv`: the bill of materials for the box and a house node (Adafruit product IDs unless
  noted; single-quantity prices in USD, November 2026; check current prices before ordering). The case is
  quoted by the machine shop and the power switch comes in a pack, so neither has a price.
- `../docs/wiring.png`, `../docs/stack.png`, `../docs/panel.png`: the wiring, the board stack-up and the
  panel layout, as drawn for the assembly guide.
- `../docs/Life_Logger_Assembly_and_Wiring_Guide.pdf`: how it goes together.

Source Location: https://github.com/sscopazzi/Life_Logger

If you make and distribute units from a modified design, CERN-OHL-S asks you to publish your modified sources
under the same license and to mark your units with the Source Location of those sources. See `../TRADEMARKS.md`
for the names and logo.
