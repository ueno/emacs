;; enwc-cm.el

;; Copyright (C) 2012,2013 Free Software Foundation

;; Author: Ian Dunn
;; Keywords: enwc, network, wicd, manager, nm

;; This file is part of ENWC

;; ENWC is free software; you can redistribute it and/or modify it
;; under the terms of the GNU General Public License as published by
;; the Free Software Foundation; either version 3, or (at your option)
;; any later version.

;; ENWC is distributed in the hope that it will be useful, but WITHOUT
;; ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
;; or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
;; License for more details.

;; You should have received a copy of the GNU General Public License
;; along with ENWC; see the file COPYING.  If not, write to the Free
;; Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
;; 02110-1301, USA.

;; connect
;; disconnect
;; scan
;; get-prop
;; save-profile
;; get-networks
;; check-connecting
;; get-current-nw-id


(require 'enwc)

(defgroup enwc-cm nil
  "*ConnMan variables for ENWC"
  :prefix "ewnc-cm-"
  :group 'enwc)

(defcustom enwc-cm-dbus-service "net.connman"
  "ConnMan D-Bus service."
  :group 'enwc-cm
  :type 'string)


;; DEMAND TO SEE LIFE'S MANAGER!!

(defcustom enwc-cm-dbus-manager-interface "net.connman.Manager"
  "ConnMan D-Bus Manager interface."
  :group 'enwc-cm
  :type 'string)

(defcustom enwc-cm-dbus-service-interface "net.connman.Service"
  "ConnMan D-Bus Service interface."
  :group 'enwc-cm
  :type 'string)

(defvar enwc-cm-details-list
  '("Name" "bssid" "Strength" "Security" "mode" "channel"))

(defun enwc-cm-get-services ()
  (dbus-call-method :system
		    enwc-cm-dbus-service
		    "/"
		    enwc-cm-dbus-manager-interface
		    "GetServices"))

(defun enwc-cm-get-networks ()
  (let ((services (enwc-cm-get-services)))
    (mapcar 'car services)))

(defun enwc-cm-get-network (id)
  (nth id (enwc-cm-get-services)))

;; scan
(defun enwc-cm-scan (wired)
  (dbus-call-method :system
		    enwc-cm-dbus-service
		    "/net/connman/technology/(ethernet|wifi)"
		    "net.connman.Technology"
		    "Scan"))

;; connect
(defun enwc-cm-connect (wired id)
  (dbus-call-method :system
		    enwc-cm-dbus-service
		    "/net/connman/service/serviceID"
		    enwc-cm-dbus-service-interface
		    "Connect"))


(defun enwc-cm-disconnect (wired)
  (dbus-call-method :system
		    enwc-cm-dbus-service
		    "/net/connman/service/CONNECTED-SERVICE"
		    enwc-cm-dbus-service-interface
		    "Disconnect"))

(defun enwc-cm-get-nw-prop (id prop)
  (let ((network (enwc-cm-get-network id)))
    (car (cadr (assoc prop (cadr network))))))

(defun enwc-cm-get-wireless-network-property (id prop)
  (enwc-cm-get-nw-prop id prop))

(defun enwc-cm-get-encryption-type (id)
  (enwc-cm-get-nw-prop id "Security"))

(defun enwc-cm-get-ip-addr (wired id)
  (let ((ipv4-config (enwc-cm-get-nw-prop id "IPv4.Configuration")))
    (car (cadr (assoc "Address" ipv4-config)))))

(defun enwc-cm-get-netmask (wired id)
  (let ((ipv4-config (enwc-cm-get-nw-prop id "IPv4.Configuration")))
    (car (cadr (assoc "Netmask" ipv4-config)))))

(defun enwc-cm-get-gateway (wired id)
  (let ((ipv4-config (enwc-cm-get-nw-prop id "IPv4.Configuration")))
    (car (cadr (assoc "Gateway" ipv4-config)))))

(defun enwc-cm-get-dns (wired id)
  (enwc-cm-get-nw-prop id "Nameservers.Configuration"))

(defun enwc-cm-set-nw-prop (wired id prop val)
  (dbus-call-method :system
		    enwc-cm-dbus-service
		    (car (enwc-cm-get-network id))
		    enwc-cm-dbus-service-interface
		    "SetProperty"
		    prop
		    val))

(defun enwc-cm-save-nw-settings (wired id settings)
  (let* ((ipv4 (enwc-cm-get-nw-prop id "IPv4.Configuration"))
	 (method (car (cadr (assoc "Method" ipv4))))
	 (ip-addr (cdr (assoc "addr" settings)))
	 (netmask (cdr (assoc "netmask" settings)))
	 (gateway (cdr (assoc "gateway" settings)))
	 new-ipv4-config new-dns-config)
    (setq new-ipv4-config
	  (list (list (cons "Method" (cons (cons method nil) nil))
		      (cons "Address" (cons (cons ip-addr nil) nil))
		      (cons "Netmask" (cons (cons netmask nil) nil))
		      (cons "Gateway" (cons (cons gateway nil) nil)))))
    (setq new-dns-config
	  (list (list (cdr (assoc "dns1" settings))
		      (cdr (assoc "dns2" settings)))))
    (enwc-cm-set-nw-prop wired id "IPv4.Configuration"
			 new-ipv4-config)
    (enwc-cm-set-nw-prop wired id "Nameservers.Configuration"
			 new-dns-config)))
