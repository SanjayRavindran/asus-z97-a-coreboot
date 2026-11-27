-- SPDX-License-Identifier: GPL-2.0-or-later

with HW.GFX.GMA;
with HW.GFX.GMA.Display_Probing;

use HW.GFX.GMA;
use HW.GFX.GMA.Display_Probing;

private package GMA.Mainboard is

   ports : constant Port_List :=
     (HDMI1,	-- DVI-D (DDI-B)
      DP2,	-- DP (DDI-C)
      HDMI2,	-- HDMI (DDI-C)
      HDMI3,	-- DVI/HDMI (DDI-D)
      Analog,	-- VGA (DDI-E)
      others => Disabled);

end GMA.Mainboard;
