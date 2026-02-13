library ieee;
use ieee.std_logic_1164.all;

-- Primitivas Xilinx
library unisim;
use unisim.vcomponents.all;

-- =====================================================
--  RTL Utility Buffer (OBUFDS) - 1 bit vectorial
-- =====================================================
entity obufds_rtl is
  port (
    OBUF_IN    : in  std_logic_vector(0 downto 0); 
    OBUF_DS_P : out std_logic_vector(0 downto 0); 
    OBUF_DS_N : out std_logic_vector(0 downto 0)   
  );
end entity obufds_rtl;

architecture rtl of obufds_rtl is
begin

  -- Buffer diferencial LVDS
  OBUFDS_inst : OBUFDS
    generic map (
      IOSTANDARD => "LVDS_25"   -- ajusta según tu banco/pines
    )
    port map (
      I  => OBUF_IN(0),
      O  => OBUF_DS_P(0),
      OB => OBUF_DS_N(0)
    );

end architecture rtl;
