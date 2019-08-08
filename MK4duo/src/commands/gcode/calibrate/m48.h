/**
 * MK4duo Firmware for 3D Printer, Laser and CNC
 *
 * Based on Marlin, Sprinter and grbl
 * Copyright (c) 2011 Camiel Gubbels / Erik van der Zalm
 * Copyright (c) 2019 Alberto Cotronei @MagoKimbra
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

/**
 * mcode
 *
 * Copyright (c) 2019 Alberto Cotronei @MagoKimbra
 */

#if ENABLED(Z_MIN_PROBE_REPEATABILITY_TEST)

  #define CODE_M48

  /**
   * M48: Z-Probe repeatability measurement function.
   *
   * Usage:
   *   M48 <P#> <X#> <Y#> <V#> <E> <L#> <S>
   *     P = Number of sampled points (4-50, default 10)
   *     X = Sample X position
   *     Y = Sample Y position
   *     V = Verbose level (0-4, default=1)
   *     E = Engage probe for each reading
   *     L = Number of legs of movement before probe
   *     S = Schizoid (Or Star if you prefer)
   *
   * This function assumes the bed has been homed.  Specifically, that a G28 command
   * as been issued prior to invoking the M48 Z-Probe repeatability measurement function.
   * Any information generated by a prior G29 Bed leveling command will be lost and need to be
   * regenerated.
   */
  inline void gcode_M48(void) {

    if (mechanics.axis_unhomed_error()) return;

    int8_t verbose_level = parser.seen('V') ? parser.value_byte() : 1;
    if (!WITHIN(verbose_level, 0, 4)) {
      SERIAL_LM(ER, "?Verbose Level not plausible (0-4).");
      return;
    }

    if (verbose_level > 0)
      SERIAL_EM("M48 Z-Probe Repeatability Test");

    int8_t n_samples = parser.seen('P') ? parser.value_byte() : 10;
    if (!WITHIN(n_samples, 4, 50)) {
      SERIAL_LM(ER, "?Sample size not plausible (4-50).");
      return;
    }

    const ProbePtRaiseEnum raise_after = parser.boolval('E') ? PROBE_PT_STOW : PROBE_PT_RAISE;

    float X_current = mechanics.current_position[X_AXIS],
          Y_current = mechanics.current_position[Y_AXIS];

    const float X_probe_location = parser.linearval('X', X_current + probe.data.offset[X_AXIS]),
                Y_probe_location = parser.linearval('Y', Y_current + probe.data.offset[Y_AXIS]);

    if (!mechanics.position_is_reachable_by_probe(X_probe_location, Y_probe_location)) {
      SERIAL_LM(ER, "? (X,Y) out of bounds.");
      return;
    }

    bool seen_L = parser.seen('L');
    uint8_t n_legs = seen_L ? parser.value_byte() : 0;
    if (n_legs > 15) {
      SERIAL_LM(ER, "?Number of legs in movement not plausible (0-15).");
      return;
    }
    if (n_legs == 1) n_legs = 2;

    bool schizoid_flag = parser.seen('S');
    if (schizoid_flag && !seen_L) n_legs = 7;

    /**
     * Now get everything to the specified probe point So we can safely do a
     * probe to get us close to the bed.  If the Z-Axis is far from the bed,
     * we don't want to use that as a starting point for each probe.
     */
    if (verbose_level > 2)
      SERIAL_EM("Positioning the probe...");

    // Disable bed level correction in M48 because we want the raw data when we probe
    #if HAS_LEVELING
      bedlevel.set_bed_leveling_enabled(false);
    #endif

    mechanics.setup_for_endstop_or_probe_move();

    float mean = 0.0, sigma = 0.0, min = 99999.9, max = -99999.9, sample_set[n_samples];

    // Move to the first point, deploy, and probe
    const float t = probe.check_pt(X_probe_location, Y_probe_location, raise_after, verbose_level);
    bool probing_good = !isnan(t);

    if (probing_good) {

      randomSeed(millis());

      for (uint8_t n = 0; n < n_samples; n++) {
        #if HAS_LCD
          // Display M48 progress in the status bar
          lcdui.status_printf_P(0, PSTR(MSG_M48_POINT ": %d/%d"), int(n + 1), int(n_samples));
        #endif
        if (n_legs) {
          const int dir = (random(0, 10) > 5.0) ? -1 : 1;  // clockwise or counter clockwise
          float angle = random(0, 360);
          const float radius = random(
            #if MECH(DELTA)
              (int)(0.1250000000 * mechanics.data.probe_radius),
              (int)(0.3333333333 * mechanics.data.probe_radius)
            #else
              5, (int)(0.125 * MIN(X_MAX_BED, Y_MAX_BED))
            #endif
          );

          if (verbose_level > 3) {
            SERIAL_MV("Starting radius: ", radius);
            SERIAL_MV("   angle: ", angle);
            SERIAL_MSG(" dir: ");
            if (dir > 0) SERIAL_CHR('C');
            SERIAL_EM("CW");
          }

          for (uint8_t l = 0; l < n_legs - 1; l++) {
            double delta_angle;

            if (schizoid_flag)
              // The points of a 5 point star are 72 degrees apart.  We need to
              // skip a point and go to the next one on the star.
              delta_angle = dir * 2.0 * 72.0;

            else
              // If we do this line, we are just trying to move further
              // around the circle.
              delta_angle = dir * (float) random(25, 45);

            angle += delta_angle;

            while (angle > 360.0)   // We probably do not need to keep the angle between 0 and 2*PI, but the
              angle -= 360.0;       // Arduino documentation says the trig functions should not be given values
            while (angle < 0.0)     // outside of this range.   It looks like they behave correctly with
              angle += 360.0;       // numbers outside of the range, but just to be safe we clamp them.

            X_current = X_probe_location - probe.data.offset[X_AXIS] + cos(RADIANS(angle)) * radius;
            Y_current = Y_probe_location - probe.data.offset[Y_AXIS] + sin(RADIANS(angle)) * radius;

            #if MECH(DELTA)
              // If we have gone out too far, we can do a simple fix and scale the numbers
              // back in closer to the origin.
              while (!mechanics.position_is_reachable_by_probe(X_current, Y_current)) {
                X_current *= 0.8;
                Y_current *= 0.8;
                if (verbose_level > 3) {
                  SERIAL_MV("Pulling point towards center:", X_current);
                  SERIAL_EMV(", ", Y_current);
                }
              }
            #else
              LIMIT(X_current, X_MIN_BED, X_MAX_BED);
              LIMIT(Y_current, Y_MIN_BED, Y_MAX_BED);
            #endif

            if (verbose_level > 3) {
              SERIAL_MSG("Going to:");
              SERIAL_MV(" X", X_current);
              SERIAL_MV(" Y", Y_current);
              SERIAL_EMV(" Z", mechanics.current_position[Z_AXIS]);
            }

            mechanics.do_blocking_move_to_xy(X_current, Y_current);
          } // n_legs loop
        } // n_legs

        // Probe a single point
        sample_set[n] = probe.check_pt(X_probe_location, Y_probe_location, raise_after, 0);

        // Break the loop if the probe fails
        probing_good = !isnan(sample_set[n]);
        if (!probing_good) break;

        /**
         * Get the current mean for the data points we have so far
         */
        double sum = 0.0;
        for (uint8_t j = 0; j <= n; j++) sum += sample_set[j];
        mean = sum / (n + 1);

        NOMORE(min, sample_set[n]);
        NOLESS(max, sample_set[n]);

        /**
         * Now, use that mean to calculate the standard deviation for the
         * data points we have so far
         */
        sum = 0.0;
        for (uint8_t j = 0; j <= n; j++)
          sum += sq(sample_set[j] - mean);

        sigma = SQRT(sum / (n + 1));
        if (verbose_level > 0) {
          if (verbose_level > 1) {
            SERIAL_VAL(n + 1);
            SERIAL_MV(" of ", n_samples);
            SERIAL_MV(": z: ", sample_set[n], 3);
            if (verbose_level > 2) {
              SERIAL_MV(" mean: ", mean, 4);
              SERIAL_MV(" sigma: ", sigma, 6);
              SERIAL_MV(" min: ", min, 3);
              SERIAL_MV(" max: ", max, 3);
              SERIAL_MV(" range: ", max - min, 3);
            }
            SERIAL_EOL();
          }
        }

      }  // End of probe loop
    }

    STOW_PROBE();

    if (probing_good) {
      SERIAL_EM("Finished!");

      if (verbose_level > 0) {
        SERIAL_MV("Mean: ", mean, 6);
        SERIAL_MV(" Min: ", min, 3);
        SERIAL_MV(" Max: ", max, 3);
        SERIAL_MV(" Range: ", max - min, 3);
        SERIAL_EOL();
      }

      SERIAL_EMV("Standard Deviation: ", sigma, 6);
      SERIAL_EOL();

      #if HAS_LCD
        // Display M48 results in the status bar
        char sigma_str[8];
        lcdui.status_printf_P(0, PSTR(MSG_M48_DEVIATION ": %s"), dtostrf(sigma, 2, 6, sigma_str));
      #endif

    }

    mechanics.clean_up_after_endstop_or_probe_move();

    // Re-enable bed level correction if it had been on
    #if HAS_LEVELING
      bedlevel.restore_bed_leveling_state();
    #endif

    mechanics.report_current_position();
  }

#endif // ENABLED(Z_MIN_PROBE_REPEATABILITY_TEST)
