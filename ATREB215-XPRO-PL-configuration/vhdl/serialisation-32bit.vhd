library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity serialisation_axis32_no_valid is
  port (
    aclk         : in  std_logic;
    aresetn      : in  std_logic;

    s_axis_tdata : in  std_logic_vector(31 downto 0);
    s_axis_tkeep : in  std_logic_vector(3 downto 0);
    s_axis_tvalid: in  std_logic;
    s_axis_tready: out std_logic;
    s_axis_tlast : in  std_logic; 

    sample_rate  : in  std_logic_vector(3 downto 0);

    data_out     : out std_logic
  );
end entity;

architecture rtl of serialisation_axis32_no_valid is

  type state_t is (ST_IDLE, ST_SHIFT, ST_ZERO_STUFF);
  signal st : state_t;

  constant IDLE_WORD : std_logic_vector(31 downto 0) := x"80004000";

  signal shreg       : std_logic_vector(31 downto 0);
  signal bit_cnt     : unsigned(5 downto 0);  -- pause counter
  signal sr_cnt      : unsigned(11 downto 0);  
  signal zero_target : unsigned(11 downto 0);
  signal ready_r     : std_logic;

  signal keep_ok     : std_logic;
  signal take_word   : std_logic;

  function sr_target_from_sample_rate(sr : std_logic_vector(3 downto 0))
    return unsigned is
  begin
    case sr is
      when "0001" => return to_unsigned(31, 12);
      when "0010" => return to_unsigned(63, 12);
      when "0011" => return to_unsigned(95, 12);
      when "0100" => return to_unsigned(127, 12);
      when "0101" => return to_unsigned(159, 12);
      when "0110" => return to_unsigned(223, 12);
      when "0111" => return to_unsigned(255, 12);
      when others => return to_unsigned(0, 12);
    end case;
  end function;

begin

  s_axis_tready <= ready_r;
  data_out      <= shreg(31);

  keep_ok   <= '1' when (s_axis_tkeep = "1111") else '0';
  take_word <= '1' when (s_axis_tvalid = '1' and ready_r = '1' and keep_ok = '1') else '0';

  process(aclk)
  begin
    if rising_edge(aclk) then
      if aresetn = '0' then
        st          <= ST_IDLE;
        shreg       <= IDLE_WORD;
        bit_cnt     <= (others => '0');
        sr_cnt      <= (others => '0');
        zero_target <= (others => '0');
        ready_r     <= '1';

      else
        case st is

          -- --------------------------------------------------
          -- If no words are received, 0x80004000 is sent
          -- --------------------------------------------------
          when ST_IDLE =>
            -- desplaza siempre
            shreg   <= shreg(30 downto 0) & '0';
            bit_cnt <= bit_cnt + 1;
            ready_r <= '1';

            -- continuously update zero_target
            zero_target <= sr_target_from_sample_rate(sample_rate);

            if bit_cnt = to_unsigned(31, 6) then
              bit_cnt <= (others => '0');

              if take_word = '1' then
                shreg   <= s_axis_tdata;
                ready_r <= '0';
                st      <= ST_SHIFT;
              else
                shreg <= IDLE_WORD;
              end if;
            end if;

          -- --------------------------------------------------
        -- SHIFT: actual word (blocks tready while 32 bits are output)
          -- --------------------------------------------------
          when ST_SHIFT =>
            ready_r <= '0';

            shreg   <= shreg(30 downto 0) & '0';
            bit_cnt <= bit_cnt + 1;

            if bit_cnt = to_unsigned(31, 6) then
              bit_cnt <= (others => '0');

              if zero_target /= to_unsigned(0, 12) then
                sr_cnt <= (others => '0');
                st     <= ST_ZERO_STUFF;
              else
                ready_r <= '1';
                st      <= ST_IDLE;
              end if;
            end if;

          -- --------------------------------------------------
          -- ZERO_STUFF: pausa con IQ válido (IDLE_WORD), NO ceros
          -- --------------------------------------------------
          when ST_ZERO_STUFF =>
            shreg <= IDLE_WORD;

            if sr_cnt < zero_target then
              sr_cnt <= sr_cnt + 1;
            else
              ready_r <= '1';
              st      <= ST_IDLE;
            end if;

        end case;
      end if;
    end if;
  end process;

end architecture;
