;;; vlf.el --- View Large Files  -*- lexical-binding: t -*-

;; Copyright (C) 2006, 2012, 2013  Free Software Foundation, Inc.

;; Version: 0.4
;; Keywords: large files, utilities
;; Authors: 2006 Mathias Dahl <mathias.dahl@gmail.com>
;;          2012 Sam Steingold <sds@gnu.org>
;;          2013 Andrey Kotlarski <m00naticus@gmail.com>

;; This file is free software; you can redistribute it and/or modify
;; it under the terms of the GNU General Public License as published by
;; the Free Software Foundation; either version 2, or (at your option)
;; any later version.

;; This file is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU General Public License for more details.

;; You should have received a copy of the GNU General Public License
;; along with GNU Emacs; see the file COPYING.  If not, write to
;; the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
;; Boston, MA 02111-1307, USA.

;;; Commentary:

;; This package provides the M-x vlf command, which visits part of a
;; large file in a read-only buffer without visiting the entire file.
;; The buffer uses VLF mode, which defines the commands M-<next>
;; (vlf-next-batch) and M-<prior> (vlf-prev-batch) to visit other
;; parts of the file.  The option `vlf-batch-size' specifies the size
;; of each batch, in bytes.

;; This package was inspired by a snippet posted by Kevin Rodgers,
;; showing how to use `insert-file-contents' to extract part of a
;; file.

;;; Code:

(defgroup vlf nil
  "View Large Files in Emacs."
  :prefix "vlf-"
  :group 'files)

(defcustom vlf-batch-size 1024
  "Defines how large each batch of file data is (in bytes)."
  :type 'integer
  :group 'vlf)

;; Keep track of file position.
(defvar vlf-start-pos)
(defvar vlf-end-pos)
(defvar vlf-file-size)

(defvar vlf-mode-map
  (let ((map (make-sparse-keymap)))
    (define-key map [M-next] 'vlf-next-batch)
    (define-key map [M-prior] 'vlf-prev-batch)
    (define-key map (kbd "M-+") 'vlf-change-batch-size)
    (define-key map (kbd "M--")
      (lambda () "Decrease vlf batch size by factor of 2."
        (interactive)
        (vlf-change-batch-size t)))
    (define-key map "s" 'vlf-re-search-forward)
    (define-key map "r" 'vlf-re-search-backward)
    (define-key map ">" (lambda () "Jump to end of file content."
                          (interactive)
                          (vlf-insert-file buffer-file-name t)))
    (define-key map "<" (lambda () "Jump to beginning of file content."
                          (interactive)
                          (vlf-insert-file buffer-file-name)))
    map)
  "Keymap for `vlf-mode'.")

(define-derived-mode vlf-mode special-mode "VLF"
  "Mode to browse large files in."
  (setq buffer-read-only t)
  (set-buffer-modified-p nil)
  (make-local-variable 'vlf-batch-size)
  (make-local-variable 'vlf-start-pos)
  (make-local-variable 'vlf-file-size))

(defun vlf-change-batch-size (decrease)
  "Change the buffer-local value of `vlf-batch-size'.
Normally, the value is doubled;
with the prefix argument DECREASE it is halved."
  (interactive "P")
  (or (assq 'vlf-batch-size (buffer-local-variables))
      (error "%s is not local in this buffer" 'vlf-batch-size))
  (setq vlf-batch-size
        (if decrease
            (/ vlf-batch-size 2)
          (* vlf-batch-size 2)))
  (vlf-update-buffer-name))

(defun vlf-format-buffer-name ()
  "Return format for vlf buffer name."
  (format "%s(%s)[%.2f%%%%](%d)"
          (file-name-nondirectory buffer-file-name)
          (file-size-human-readable vlf-file-size)
          (/ (* 100 vlf-end-pos) (float vlf-file-size))
          vlf-batch-size))

(defun vlf-update-buffer-name ()
  "Update the current buffer name."
  (rename-buffer (vlf-format-buffer-name) t))

(defun vlf-next-batch (append)
  "Display the next batch of file data.
When prefix argument is supplied and positive
 jump over APPEND number of batches.
When prefix argument is negative
 append next APPEND number of batches to the existing buffer."
  (interactive "p")
  (let ((end (+ vlf-end-pos (* vlf-batch-size
                                (abs append)))))
    (when (< vlf-file-size end)		; re-check file size
      (setq vlf-file-size (nth 7 (file-attributes buffer-file-name)))
      (cond ((= vlf-end-pos vlf-file-size)
             (error "Already at EOF"))
            ((< vlf-file-size end)
             (setq end vlf-file-size))))
    (let ((inhibit-read-only t)
          (do-append (< append 0))
          (pos (point)))
      (if do-append
          (goto-char (point-max))
        (setq vlf-start-pos (- end vlf-batch-size))
        (erase-buffer))
      (insert-file-contents buffer-file-name nil
                            (if do-append
                                vlf-end-pos
                              vlf-start-pos)
                            end)
      (goto-char pos))
    (setq vlf-end-pos end))
  (set-buffer-modified-p nil)
  (vlf-update-buffer-name))

(defun vlf-prev-batch (prepend)
  "Display the previous batch of file data.
When prefix argument is supplied and positive
 jump over PREPEND number of batches.
When prefix argument is negative
 append previous PREPEND number of batches to the existing buffer."
  (interactive "p")
  (if (zerop vlf-start-pos)
      (error "Already at BOF"))
  (let ((inhibit-read-only t)
        (start (max 0 (- vlf-start-pos (* vlf-batch-size
                                           (abs prepend)))))
        (do-prepend (< prepend 0))
        (pos (- (point-max) (point))))
    (if do-prepend
        (goto-char (point-min))
      (setq vlf-end-pos (+ start vlf-batch-size))
      (erase-buffer))
    (insert-file-contents buffer-file-name nil start
                          (if do-prepend
                              vlf-start-pos
                            vlf-end-pos))
    (goto-char (- (point-max) pos))
    (setq vlf-start-pos start))
  (set-buffer-modified-p nil)
  (vlf-update-buffer-name))

(defun vlf-move-to-chunk (start end)
  "Move to chunk determined by START END."
  (if (< vlf-file-size end)          ; re-check file size
      (setq vlf-file-size (nth 7
                                (file-attributes buffer-file-name))))
  (setq vlf-start-pos (max 0 start)
        vlf-end-pos (min end vlf-file-size))
  (let ((inhibit-read-only t))
    (erase-buffer)
    (insert-file-contents buffer-file-name nil
                          vlf-start-pos vlf-end-pos))
  (set-buffer-modified-p nil)
  (vlf-update-buffer-name))

(defun vlf-insert-file (file &optional from-end)
  "Insert first chunk of FILE contents in current buffer.
With FROM-END prefix, start from the back."
  (if from-end
      (setq vlf-start-pos (max 0 (- vlf-file-size vlf-batch-size))
            vlf-end-pos vlf-file-size)
    (setq vlf-start-pos 0
          vlf-end-pos (min vlf-batch-size vlf-file-size)))
  (vlf-move-to-chunk vlf-start-pos vlf-end-pos))

;;;###autoload
(defun vlf (file &optional from-end)
  "View Large FILE.  With FROM-END prefix, view from the back.
Batches of the file data from FILE will be displayed in a read-only
buffer.  You can customize number of bytes displayed by customizing
`vlf-batch-size'."
  (interactive "fFile to open: \nP")
  (with-current-buffer (generate-new-buffer "*vlf*")
    (buffer-disable-undo)
    (setq buffer-file-name file
          vlf-file-size (nth 7 (file-attributes file)))
    (vlf-insert-file file from-end)
    (vlf-mode)
    (switch-to-buffer (current-buffer))))

;;;###autoload
(defun dired-vlf (from-end)
  "In Dired, visit the file on this line in VLF mode.
With FROM-END prefix, view from the back."
  (interactive "P")
  (vlf (dired-get-file-for-visit) from-end))

;;;###autoload
(eval-after-load "dired"
  '(define-key dired-mode-map "V" 'dired-vlf))

;;;###autoload
(defun vlf-if-file-too-large (size op-type &optional filename)
  "If file SIZE larger than `large-file-warning-threshold', \
allow user to view file with `vlf', open it normally or abort.
OP-TYPE specifies the file operation being performed over FILENAME."
  (and large-file-warning-threshold size
       (> size large-file-warning-threshold)
       (let ((char nil))
         (while (not (memq (setq char
                                 (read-event
                                  (propertize
                                   (format "File %s is large (%s): \
%s normally (o), %s with vlf (v) or abort (a)"
                                           (file-name-nondirectory filename)
                                           (file-size-human-readable size)
                                           op-type op-type)
                                   'face 'minibuffer-prompt)))
                           '(?o ?O ?v ?V ?a ?A))))
         (cond ((memq char '(?o ?O)))
               ((memq char '(?v ?V))
                (vlf nil filename)
                (error ""))
               ((memq char '(?a ?A))
                (error "Aborted"))))))

;;; hijack `abort-if-file-too-large'
;;;###autoload
(fset 'abort-if-file-too-large 'vlf-if-file-too-large)

(defun vlf-re-search-forward (regexp count)
  "Search forward for REGEXP prefix COUNT number of times."
  (interactive (list (read-regexp "Search whole file"
                                  (if regexp-history
                                      (car regexp-history))
                                  'regexp-history)
                     (or current-prefix-arg 1)))
  (let ((match-chunk-start vlf-start-pos)
        (match-chunk-end vlf-end-pos)
        (match-start-pos (point))
        (match-end-pos (point))
        (to-find count)
        (search-reporter (make-progress-reporter
                          (concat "Searching for " regexp)
                          vlf-start-pos vlf-file-size))
        (initial-chunk t))
    (unwind-protect
        (catch 'end-of-file
          (while (not (zerop to-find))
            (cond ((re-search-forward regexp nil t)
                   (setq to-find (if (= match-start-pos
                                        (match-beginning 0))
                                     to-find
                                   (1- to-find))
                         match-start-pos (match-beginning 0)
                         match-end-pos (match-end 0)
                         match-chunk-start vlf-start-pos
                         match-chunk-end vlf-end-pos)
                   (if (and (< vlf-batch-size match-start-pos)
                            (> (- vlf-end-pos vlf-start-pos)
                               vlf-batch-size))
                       (setq match-chunk-start
                             (+ match-chunk-start vlf-batch-size)
                             match-start-pos (- match-start-pos
                                                vlf-batch-size)
                             match-end-pos (- match-end-pos
                                              vlf-batch-size))))
                  ((= vlf-end-pos vlf-file-size)
                   (throw 'end-of-file nil))
                  (t (if initial-chunk
                         (progn (setq initial-chunk nil)
                                (vlf-next-batch -1))
                       (vlf-move-to-chunk (+ vlf-start-pos
                                              vlf-batch-size)
                                           (+ vlf-end-pos
                                              vlf-batch-size)))
                     (goto-char (if (< vlf-start-pos match-chunk-end)
                                    match-start-pos
                                  (point-min)))
                     (goto-char match-start-pos)
                     (progress-reporter-update search-reporter
                                               vlf-end-pos))))
          (progress-reporter-done search-reporter))
      (vlf-end-search match-chunk-start match-chunk-end
                       match-end-pos count to-find))))

(defun vlf-re-search-backward (regexp count)
  "Search backward for REGEXP prefix COUNT number of times."
  (interactive (list (read-regexp "Search whole file"
                                  (if regexp-history
                                      (car regexp-history))
                                  'regexp-history)
                     (or current-prefix-arg 1)))
  (let ((match-chunk-start vlf-start-pos)
        (match-chunk-end vlf-end-pos)
        (match-start-pos (point))
        (match-end-pos (point))
        (to-find count)
        (search-reporter (make-progress-reporter
                          (concat "Searching for " regexp)
                          (- vlf-file-size vlf-end-pos)
                          vlf-file-size))
        (initial-chunk t))
    (unwind-protect
        (catch 'start-of-file
          (while (not (zerop to-find))
            (cond ((re-search-backward regexp nil t)
                   (setq to-find (if (= match-end-pos
                                        (match-end 0))
                                     to-find
                                   (1- to-find))
                         match-start-pos (match-beginning 0)
                         match-end-pos (match-end 0)
                         match-chunk-start vlf-start-pos
                         match-chunk-end vlf-end-pos)
                   (if (and (< match-end-pos vlf-batch-size)
                            (> (- vlf-end-pos vlf-start-pos)
                               vlf-batch-size))
                       (setq match-chunk-end
                             (- match-chunk-end
                                vlf-batch-size))))
                  ((zerop vlf-start-pos)
                   (throw 'start-of-file nil))
                  (t (if initial-chunk
                         (progn (setq initial-chunk nil)
                                (vlf-prev-batch -1))
                       (vlf-move-to-chunk (- vlf-start-pos
                                              vlf-batch-size)
                                           (- vlf-end-pos
                                              vlf-batch-size)))
                     (goto-char (if (< match-chunk-start vlf-end-pos)
                                    match-end-pos
                                  (point-max)))
                     (setq last-chunk-match nil)
                     (progress-reporter-update search-reporter
                                               (- vlf-file-size
                                                  vlf-start-pos)))))
          (progress-reporter-done search-reporter))
      (vlf-end-search match-chunk-start match-chunk-end
                       match-start-pos count to-find))))

(defun vlf-end-search (match-chunk-start match-chunk-end
                                          match-pos count to-find)
  "Move to chunk determined by MATCH-CHUNK-START and MATCH-CHUNK-END.
Go to MATCH-POS and according to COUNT and left TO-FIND show if search
has been successful.  Return nil if nothing found."
  (vlf-move-to-chunk match-chunk-start match-chunk-end)
  (goto-char match-pos)
  (cond ((zerop to-find) t)
        ((< to-find count)
         (message "Moved to the %d match which is last found"
                  (- count to-find))
         t)
        (t (message "Not found")
           nil)))

(provide 'vlf)

;;; vlf.el ends here
