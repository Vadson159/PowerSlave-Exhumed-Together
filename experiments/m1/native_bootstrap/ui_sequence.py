"""Bounded native-menu key sequence through M0's owned-HWND helper only."""
import sys
import time
import ui


def main():
    run, keys = sys.argv[1], sys.argv[2:]
    if len(keys) > 8 or any(k not in ['RETURN', 'DOWN', 'UP', 'LEFT', 'RIGHT', 'ESC', 'W', 'S', 'A', 'D'] for k in keys):
        raise ValueError('Unsupported bounded UI sequence')
    for key in keys:
        sys.argv = ['ui', run, 'key', '--key', key]
        ui.main()
        time.sleep(0.3)
    time.sleep(0.25)
    sys.argv = ['ui', run, 'capture']
    ui.main()


if __name__ == '__main__':
    main()
