/*
 * Minimal zstream shim for CheckPilot.
 * This avoids hard failures when bundled pako sources are missing.
 */

export default class ZStream {
    constructor() {
        this.input = null;
        this.next_in = 0;
        this.avail_in = 0;
        this.output = null;
        this.next_out = 0;
        this.avail_out = 0;
    }
}
